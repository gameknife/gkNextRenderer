#include "Engine/Runtime/Components/SkinnedMeshComponent.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Utilities/NextEngineHelper.hpp"
#include "Engine/Runtime/Reflection/PropertyMeta.hpp"
#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Loaders/OzzAnimationBuilder.hpp"
#include <algorithm>
#include <functional>
#include <unordered_map>
#include <entt/meta/factory.hpp>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cstring>

#include "ozz/animation/runtime/animation.h"
#include "ozz/animation/runtime/blending_job.h"
#include "ozz/animation/runtime/local_to_model_job.h"
#include "ozz/animation/runtime/sampling_job.h"
#include "ozz/animation/runtime/skeleton.h"
#include "ozz/base/maths/simd_quaternion.h"
#include "ozz/base/containers/vector.h"
#include "ozz/base/maths/simd_math.h"
#include "ozz/base/maths/soa_transform.h"
#include "ozz/base/memory/unique_ptr.h"
#include "ozz/base/platform.h"

#include "Engine/Runtime/Components/SkinnedMeshComponent.Internal.hpp"
namespace Runtime
{
    void SkinnedMeshComponent::RegisterReflection()
    {
        using namespace entt::literals;
        using namespace Reflection;
        
        entt::meta_factory<SkinnedMeshComponent>()
            .type("SkinnedMeshComponent"_hs)
            // PlaySpeed property - editable (use string literal for name)
            .data<&SkinnedMeshComponent::SetPlaySpeed, &SkinnedMeshComponent::GetPlaySpeed>("PlaySpeed")
                .custom<PropertyMeta>(PropertyPresets::Range("Play Speed", "Animation", 0.0f, 10.0f, "Animation playback speed multiplier"))
            // IsPlaying property - read-only
            .data<nullptr, &SkinnedMeshComponent::GetIsPlaying>("IsPlaying")
                .custom<PropertyMeta>(PropertyPresets::ReadOnly("Is Playing", "Animation", "Whether an animation is currently playing"))
            // CurrentAnimationName - read-only (use PlayAnimation to change)
            .data<nullptr, &SkinnedMeshComponent::GetCurrentAnimationName>("CurrentAnimation")
                .custom<PropertyMeta>(PropertyPresets::ReadOnly("Current Animation", "Animation", "Name of the currently playing animation"))
            // Methods for JS
            .func<&SkinnedMeshComponent::PlayAnimation>("PlayAnimation")
            .func<&SkinnedMeshComponent::StopAnimation>("StopAnimation")
            .func<&SkinnedMeshComponent::GetAnimationNames>("GetAnimationNames");
    }

    SkinnedMeshComponent::SkinnedMeshComponent(const Assets::Skeleton& skeleton)
        : skeleton_(skeleton)
        , ozz_(std::make_unique<SkinnedMeshOzzState>())
    {
        runtimeJoints_.resize(skeleton_.Joints.size());
        jointMatrices_.resize(skeleton_.Joints.size(), glm::mat4(1.0f));

        for (size_t i = 0; i < skeleton_.Joints.size(); ++i)
        {
            jointMap_[skeleton_.Joints[i].Name] = static_cast<int>(i);
            runtimeJoints_[i].Translation = skeleton_.Joints[i].Translation;
            runtimeJoints_[i].Rotation = skeleton_.Joints[i].Rotation;
            runtimeJoints_[i].Scale = skeleton_.Joints[i].Scale;
        }

        UpdateJoints();

        currentState_.Playing = false;
        currentState_.CurrentTime = 0.0f;
        blendSourceState_.Playing = false;
        blendSourceState_.CurrentTime = 0.0f;

        ozz_->skeleton = Assets::BuildOzzSkeleton(skeleton_);
        if (ozz_->skeleton)
        {
            const int numJoints = ozz_->skeleton->num_joints();
            const int numSoa = ozz_->skeleton->num_soa_joints();
            ozz_->contextA.Resize(numJoints);
            ozz_->contextB.Resize(numJoints);
            ozz_->localsA.resize(numSoa);
            ozz_->localsB.resize(numSoa);
            ozz_->blended.resize(numSoa);
            ozz_->models.resize(numJoints);

            // Build mapping ozz_index -> assets_index using joint names.
            ozz_->ozzToAsset.assign(numJoints, -1);
            const auto names = ozz_->skeleton->joint_names();
            for (int i = 0; i < numJoints; ++i)
            {
                const auto it = jointMap_.find(names[i]);
                if (it != jointMap_.end())
                {
                    ozz_->ozzToAsset[i] = it->second;
                }
                else
                {
                    SPDLOG_WARN("SkinnedMeshComponent: ozz joint '{}' has no match in Assets::Skeleton", names[i]);
                }
            }
        }
        else
        {
            SPDLOG_ERROR("SkinnedMeshComponent: BuildOzzSkeleton failed for skeleton '{}'", skeleton_.Name);
        }
    }

    SkinnedMeshComponent::~SkinnedMeshComponent() = default;

    void SkinnedMeshComponent::AddAnimations(const std::vector<Assets::AnimationTrack>& allTracks)
    {
        for (const auto& track : allTracks)
        {
            if (jointMap_.find(track.NodeName_) != jointMap_.end())
            {
                animations_[track.AnimationName].push_back(track);
            }
        }
    }

    void SkinnedMeshComponent::PlayAnimation(const std::string& name, bool loop)
    {
        auto animIt = animations_.find(name);
        if (animIt == animations_.end())
        {
            SPDLOG_WARN("Animation '{}' not found", name);
            return;
        }

        AnimationState nextState {};
        nextState.Name = name;
        nextState.Loop = loop;
        nextState.CurrentTime = 0.0f;
        nextState.Playing = true;
        nextState.PlaySpeed = currentState_.PlaySpeed;
        nextState.Duration = 0.0f;
        for (const auto& track : animIt->second)
        {
            nextState.Duration = std::max(nextState.Duration, track.Duration_);
        }

        if (!currentState_.Name.empty() && currentState_.Name != name)
        {
            blendSourceState_ = currentState_;
            blendActive_ = true;
            blendElapsed_ = 0.0f;
        }
        else
        {
            blendActive_ = false;
        }

        currentState_ = nextState;
    }

    void SkinnedMeshComponent::StopAnimation()
    {
        currentState_.Playing = false;
        blendActive_ = false;
    }

    std::vector<std::string> SkinnedMeshComponent::GetAnimationNames() const
    {
        std::vector<std::string> names;
        for (const auto& pair : animations_)
        {
            names.push_back(pair.first);
        }
        return names;
    }

    void SkinnedMeshComponent::Update(float deltaTime)
    {
        if (currentState_.Name.empty())
        {
            return;
        }

        AdvanceAnimationState(currentState_, deltaTime);

        const bool sampledCurrent = SampleOzz(currentState_, 0);
        float currentWeight = 1.0f;

        if (blendActive_)
        {
            AdvanceAnimationState(blendSourceState_, deltaTime);
            SampleOzz(blendSourceState_, 1);
            blendElapsed_ += deltaTime;
            currentWeight = glm::clamp(blendElapsed_ / blendDuration_, 0.0f, 1.0f);
            if (currentWeight >= 1.0f)
            {
                blendActive_ = false;
            }
        }

        if (sampledCurrent)
        {
            FinalizePose(currentWeight);
        }

    }

    void SkinnedMeshComponent::DrawDebugSkeleton(const glm::mat4& worldTransform)
    {
        for (size_t i = 0; i < skeleton_.Joints.size(); ++i)
        {
            int parentIdx = skeleton_.Joints[i].ParentIndex;
            if (parentIdx != -1)
            {
                glm::vec3 start = glm::vec3(worldTransform * runtimeJoints_[parentIdx].GlobalTransform * glm::vec4(0, 0, 0, 1));
                glm::vec3 end = glm::vec3(worldTransform * runtimeJoints_[i].GlobalTransform * glm::vec4(0, 0, 0, 1));
                
                Runtime::EngineHelper::DrawAuxLine(start, end, glm::vec4(0, 1, 0, 1), 2.0f);
            }
        }
    }

    void SkinnedMeshComponent::UpdateJoints()
    {
        std::function<void(int, const glm::mat4&)> traverse;
        traverse = [&](int idx, const glm::mat4& parentXform) {
            auto& rtJoint = runtimeJoints_[idx];
            
            glm::mat4 local = glm::translate(glm::mat4(1.0f), rtJoint.Translation) *
                              glm::toMat4(rtJoint.Rotation) *
                              glm::scale(glm::mat4(1.0f), rtJoint.Scale);
            
            rtJoint.GlobalTransform = parentXform * local;
            
            jointMatrices_[idx] = rtJoint.GlobalTransform * skeleton_.Joints[idx].InverseBindMatrix;
            
            for (int child : skeleton_.Joints[idx].Children)
            {
                traverse(child, rtJoint.GlobalTransform);
            }
        };
        
        for (size_t i = 0; i < skeleton_.Joints.size(); ++i)
        {
            if (skeleton_.Joints[i].ParentIndex == -1)
            {
                traverse(static_cast<int>(i), glm::mat4(1.0f));
            }
        }
    }

    bool SkinnedMeshComponent::SampleOzz(const AnimationState& state, int contextSlot)
    {
        if (!ozz_ || !ozz_->skeleton)
        {
            return false;
        }

        // Lazy-build ozz animation for this state.Name if not yet cached.
        auto it = ozz_->animations.find(state.Name);
        if (it == ozz_->animations.end())
        {
            auto animIt = animations_.find(state.Name);
            if (animIt == animations_.end())
            {
                return false;
            }
            auto built = Assets::BuildOzzAnimation(state.Name, *ozz_->skeleton, animIt->second);
            if (!built)
            {
                return false;
            }
            it = ozz_->animations.emplace(state.Name, std::move(built)).first;
        }

        const ozz::animation::Animation* animation = it->second.get();
        const float duration = animation->duration();
        const float ratio = duration > 0.0f ? glm::clamp(state.CurrentTime / duration, 0.0f, 1.0f) : 0.0f;

        ozz::animation::SamplingJob job;
        job.animation = animation;
        job.context = contextSlot == 0 ? &ozz_->contextA : &ozz_->contextB;
        job.ratio = ratio;
        job.output = ozz::make_span(contextSlot == 0 ? ozz_->localsA : ozz_->localsB);

        if (!job.Run())
        {
            SPDLOG_ERROR("SkinnedMeshComponent: SamplingJob failed for '{}'", state.Name);
            return false;
        }
        return true;
    }

    void SkinnedMeshComponent::FinalizePose(float currentWeight)
    {
        if (!ozz_ || !ozz_->skeleton)
        {
            return;
        }

        // Blend (or copy) into ozz_->blended.
        if (blendActive_ && currentWeight < 1.0f)
        {
            ozz::animation::BlendingJob::Layer layers[2];
            layers[0].weight = currentWeight;
            layers[0].transform = ozz::make_span(ozz_->localsA);
            layers[1].weight = 1.0f - currentWeight;
            layers[1].transform = ozz::make_span(ozz_->localsB);

            ozz::animation::BlendingJob blend;
            blend.threshold = 0.1f;
            blend.layers = ozz::span<const ozz::animation::BlendingJob::Layer>(layers, layers + 2);
            blend.rest_pose = ozz_->skeleton->joint_rest_poses();
            blend.output = ozz::make_span(ozz_->blended);
            if (!blend.Run())
            {
                SPDLOG_ERROR("SkinnedMeshComponent: BlendingJob failed");
                return;
            }
        }
        else
        {
            // Pure current pose; memcpy is the cheapest option.
            const size_t bytes = ozz_->localsA.size() * sizeof(ozz::math::SoaTransform);
            std::memcpy(ozz_->blended.data(), ozz_->localsA.data(), bytes);
        }

        ozz::animation::LocalToModelJob ltm;
        ltm.skeleton = ozz_->skeleton.get();
        ltm.input = ozz::make_span(ozz_->blended);
        ltm.output = ozz::make_span(ozz_->models);
        if (!ltm.Run())
        {
            SPDLOG_ERROR("SkinnedMeshComponent: LocalToModelJob failed");
            return;
        }

        // Mirror ozz output back into runtimeJoints_ / jointMatrices_ in Assets joint order.
        const int numJoints = ozz_->skeleton->num_joints();

        auto extractLane = [](const ozz::math::SimdFloat4& v, int lane) -> float
        {
            alignas(16) float tmp[4];
            ozz::math::StorePtr(v, tmp);
            return tmp[lane];
        };

        for (int ozzI = 0; ozzI < numJoints; ++ozzI)
        {
            const int assetI = ozz_->ozzToAsset[ozzI];
            if (assetI < 0)
            {
                continue;
            }

            const ozz::math::SoaTransform& soa = ozz_->blended[ozzI / 4];
            const int lane = ozzI % 4;

            RuntimeJoint& rj = runtimeJoints_[assetI];
            rj.Translation = glm::vec3(
                extractLane(soa.translation.x, lane),
                extractLane(soa.translation.y, lane),
                extractLane(soa.translation.z, lane));
            rj.Rotation = glm::quat(
                extractLane(soa.rotation.w, lane),
                extractLane(soa.rotation.x, lane),
                extractLane(soa.rotation.y, lane),
                extractLane(soa.rotation.z, lane));
            rj.Scale = glm::vec3(
                extractLane(soa.scale.x, lane),
                extractLane(soa.scale.y, lane),
                extractLane(soa.scale.z, lane));

            const ozz::math::Float4x4& m = ozz_->models[ozzI];
            alignas(16) float colData[16];
            ozz::math::StorePtr(m.cols[0], colData + 0);
            ozz::math::StorePtr(m.cols[1], colData + 4);
            ozz::math::StorePtr(m.cols[2], colData + 8);
            ozz::math::StorePtr(m.cols[3], colData + 12);
            const glm::mat4 global = glm::make_mat4(colData);
            rj.GlobalTransform = global;
            jointMatrices_[assetI] = global * skeleton_.Joints[assetI].InverseBindMatrix;
        }
    }

    void SkinnedMeshComponent::AdvanceAnimationState(AnimationState& state, float deltaTime) const
    {
        if (!state.Playing)
        {
            return;
        }

        state.CurrentTime += deltaTime * state.PlaySpeed;

        if (state.CurrentTime > state.Duration)
        {
            if (state.Loop && state.Duration > 0.0f)
            {
                state.CurrentTime = fmod(state.CurrentTime, state.Duration);
            }
            else
            {
                state.CurrentTime = state.Duration;
                state.Playing = false;
            }
        }
        else if (state.CurrentTime < 0.0f)
        {
            if (state.Loop && state.Duration > 0.0f)
            {
                state.CurrentTime = state.Duration + fmod(state.CurrentTime, state.Duration);
            }
            else
            {
                state.CurrentTime = 0.0f;
                state.Playing = false;
            }
        }
    }

}
