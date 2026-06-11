#include "SkinnedMeshComponent.h"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Utilities/NextEngineHelper.h"
#include "Engine/Runtime/Reflection/PropertyMeta.h"
#include "Engine/Assets/Core/Node.h"
#include "Engine/Assets/Loaders/OzzAnimationBuilder.h"
#include <algorithm>
#include <functional>
#include <unordered_map>
#include <entt/meta/factory.hpp>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cstring>

#if WITH_OZZ
#include "ozz/animation/runtime/animation.h"
#include "ozz/animation/runtime/blending_job.h"
#include "ozz/animation/runtime/ik_aim_job.h"
#include "ozz/animation/runtime/ik_two_bone_job.h"
#include "ozz/animation/runtime/local_to_model_job.h"
#include "ozz/animation/runtime/sampling_job.h"
#include "ozz/animation/runtime/skeleton.h"
#include "ozz/base/maths/simd_quaternion.h"
#include "ozz/base/containers/vector.h"
#include "ozz/base/maths/simd_math.h"
#include "ozz/base/maths/soa_transform.h"
#include "ozz/base/memory/unique_ptr.h"
#include "ozz/base/platform.h"
#endif

namespace Runtime
{
#if WITH_OZZ
    struct SkinnedMeshOzzState
    {
        ozz::unique_ptr<ozz::animation::Skeleton> skeleton;
        std::unordered_map<std::string, ozz::unique_ptr<ozz::animation::Animation>> animations;

        ozz::animation::SamplingJob::Context contextA;
        ozz::animation::SamplingJob::Context contextB;

        ozz::vector<ozz::math::SoaTransform> localsA;
        ozz::vector<ozz::math::SoaTransform> localsB;
        ozz::vector<ozz::math::SoaTransform> blended;
        ozz::vector<ozz::math::Float4x4> models;

        // Mapping from ozz joint index (depth-first) to the matching Assets::Skeleton::Joints[] index.
        // -1 means no matching joint by name (should not happen in practice).
        std::vector<int> ozzToAsset;
    };
#else
    struct SkinnedMeshOzzState {};
#endif
}

namespace Runtime
{
    namespace
    {
        std::string NormalizeJointName(std::string_view name)
        {
            std::string normalized;
            normalized.reserve(name.size());
            for (char ch : name)
            {
                if (ch == ' ' || ch == '_' || ch == '-' || ch == '.')
                {
                    continue;
                }
                normalized.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
            }
            return normalized;
        }

        float DampToward(float current, float target, float sharpness, float deltaTime)
        {
            if (sharpness <= 0.0f)
            {
                return target;
            }

            const float alpha = 1.0f - std::exp(-sharpness * deltaTime);
            return glm::mix(current, target, glm::clamp(alpha, 0.0f, 1.0f));
        }

        glm::vec3 ProjectOntoPlane(const glm::vec3& vector, const glm::vec3& planeNormal)
        {
            return vector - planeNormal * glm::dot(vector, planeNormal);
        }

        float SignedAngleAroundAxis(const glm::vec3& from, const glm::vec3& to, const glm::vec3& axis)
        {
            const glm::vec3 fromDir = glm::normalize(from);
            const glm::vec3 toDir = glm::normalize(to);
            const glm::vec3 axisDir = glm::normalize(axis);
            return std::atan2(glm::dot(axisDir, glm::cross(fromDir, toDir)), glm::dot(fromDir, toDir));
        }

#if WITH_OZZ
        ozz::math::Float4x4 ToOzzMat4(const glm::mat4& m)
        {
            ozz::math::Float4x4 out;
            // glm::mat4 is column-major; ozz Float4x4 cols are SimdFloat4 in same layout.
            out.cols[0] = ozz::math::simd_float4::LoadPtrU(glm::value_ptr(m) + 0);
            out.cols[1] = ozz::math::simd_float4::LoadPtrU(glm::value_ptr(m) + 4);
            out.cols[2] = ozz::math::simd_float4::LoadPtrU(glm::value_ptr(m) + 8);
            out.cols[3] = ozz::math::simd_float4::LoadPtrU(glm::value_ptr(m) + 12);
            return out;
        }

        ozz::math::SimdFloat4 LoadVec3(const glm::vec3& v)
        {
            alignas(16) const float tmp[4] = {v.x, v.y, v.z, 0.0f};
            return ozz::math::simd_float4::LoadPtr(tmp);
        }

        glm::quat SimdQuatToGlm(const ozz::math::SimdQuaternion& q)
        {
            alignas(16) float tmp[4];
            ozz::math::StorePtr(q.xyzw, tmp);
            return glm::quat(tmp[3], tmp[0], tmp[1], tmp[2]);
        }
#endif
    }

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

#if WITH_OZZ
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
#endif
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

#if WITH_OZZ
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
#endif

        ApplyFootPlacementIK(deltaTime);
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

#if WITH_OZZ
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
#endif

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

    int SkinnedMeshComponent::FindJointIndex(std::initializer_list<std::string_view> aliases,
                                             std::initializer_list<std::string_view> containsTokens) const
    {
        for (const auto& joint : jointMap_)
        {
            const std::string normalized = NormalizeJointName(joint.first);
            for (std::string_view alias : aliases)
            {
                if (normalized == NormalizeJointName(alias))
                {
                    return joint.second;
                }
            }
        }

        for (const auto& joint : jointMap_)
        {
            const std::string normalized = NormalizeJointName(joint.first);
            bool matches = !containsTokens.size();
            for (std::string_view token : containsTokens)
            {
                if (normalized.find(NormalizeJointName(token)) == std::string::npos)
                {
                    matches = false;
                    break;
                }
                matches = true;
            }

            if (matches)
            {
                return joint.second;
            }
        }

        return -1;
    }

    glm::quat SkinnedMeshComponent::ExtractJointGlobalRotation(int jointIndex) const
    {
        if (jointIndex < 0 || jointIndex >= static_cast<int>(runtimeJoints_.size()))
        {
            return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        }
        return glm::normalize(glm::quat_cast(glm::mat3(runtimeJoints_[jointIndex].GlobalTransform)));
    }

    glm::quat SkinnedMeshComponent::MakeRotationBetween(const glm::vec3& from, const glm::vec3& to) const
    {
        const float fromLen = glm::length(from);
        const float toLen = glm::length(to);
        if (fromLen < 1e-5f || toLen < 1e-5f)
        {
            return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        }

        const glm::vec3 fromDir = from / fromLen;
        const glm::vec3 toDir = to / toLen;
        const float cosTheta = glm::clamp(glm::dot(fromDir, toDir), -1.0f, 1.0f);
        if (cosTheta > 0.9999f)
        {
            return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        }
        if (cosTheta < -0.9999f)
        {
            glm::vec3 axis = glm::cross(fromDir, glm::vec3(0.0f, 1.0f, 0.0f));
            if (glm::length2(axis) < 1e-5f)
            {
                axis = glm::cross(fromDir, glm::vec3(1.0f, 0.0f, 0.0f));
            }
            axis = glm::normalize(axis);
            return glm::angleAxis(glm::pi<float>(), axis);
        }

        return glm::normalize(glm::rotation(fromDir, toDir));
    }

    bool SkinnedMeshComponent::ResolveFootPlacementChains()
    {
        if (footPlacementChainsResolved_)
        {
            return footPlacementChainsValid_;
        }

        footPlacementChainsResolved_ = true;
        hipsJointIndex_ = FindJointIndex({"hips", "pelvis"},
                                         {"hip"});
        if (hipsJointIndex_ == -1)
        {
            hipsJointIndex_ = FindJointIndex({"root"}, {"spine", "base"});
        }

        leftFootPlacementChain_.upperLeg = FindJointIndex({"upperleg.l", "thigh.l", "upleg.l", "upperleg_l", "thigh_l"},
                                                          {"left", "thigh"});
        leftFootPlacementChain_.lowerLeg = FindJointIndex({"lowerleg.l", "calf.l", "leg.l", "lowerleg_l", "calf_l"},
                                                          {"left", "calf"});
        leftFootPlacementChain_.foot = FindJointIndex({"foot.l", "ankle.l", "foot_l", "ankle_l"},
                                                      {"left", "foot"});
        leftFootPlacementChain_.toe = FindJointIndex({"toes.l", "toe.l", "ball.l", "toes_l", "toe_l", "ball_l"},
                                                     {"left", "toe"});

        rightFootPlacementChain_.upperLeg = FindJointIndex({"upperleg.r", "thigh.r", "upleg.r", "upperleg_r", "thigh_r"},
                                                           {"right", "thigh"});
        rightFootPlacementChain_.lowerLeg = FindJointIndex({"lowerleg.r", "calf.r", "leg.r", "lowerleg_r", "calf_r"},
                                                           {"right", "calf"});
        rightFootPlacementChain_.foot = FindJointIndex({"foot.r", "ankle.r", "foot_r", "ankle_r"},
                                                       {"right", "foot"});
        rightFootPlacementChain_.toe = FindJointIndex({"toes.r", "toe.r", "ball.r", "toes_r", "toe_r", "ball_r"},
                                                      {"right", "toe"});

        auto buildBindGlobals = [this]()
        {
            std::vector<glm::mat4> bindGlobals(skeleton_.Joints.size(), glm::mat4(1.0f));
            std::function<void(int, const glm::mat4&)> traverse = [&](int idx, const glm::mat4& parentTransform)
            {
                const Assets::Joint& joint = skeleton_.Joints[idx];
                const glm::mat4 localTransform =
                    glm::translate(glm::mat4(1.0f), joint.Translation) *
                    glm::toMat4(joint.Rotation) *
                    glm::scale(glm::mat4(1.0f), joint.Scale);
                bindGlobals[idx] = parentTransform * localTransform;
                for (int child : joint.Children)
                {
                    traverse(child, bindGlobals[idx]);
                }
            };

            for (size_t i = 0; i < skeleton_.Joints.size(); ++i)
            {
                if (skeleton_.Joints[i].ParentIndex == -1)
                {
                    traverse(static_cast<int>(i), glm::mat4(1.0f));
                }
            }
            return bindGlobals;
        };

        auto initializeFootAxes = [](FootPlacementChain& chain, const std::vector<glm::mat4>& bindGlobals)
        {
            if (chain.foot == -1 || chain.lowerLeg == -1 || chain.toe == -1)
            {
                return;
            }

            const glm::vec3 lowerBindPos = glm::vec3(bindGlobals[chain.lowerLeg][3]);
            const glm::vec3 footBindPos = glm::vec3(bindGlobals[chain.foot][3]);
            const glm::vec3 toeBindPos = glm::vec3(bindGlobals[chain.toe][3]);

            glm::vec3 bindForward = toeBindPos - footBindPos;
            glm::vec3 bindDown = footBindPos - lowerBindPos;
            if (glm::length2(bindForward) < 1e-5f || glm::length2(bindDown) < 1e-5f)
            {
                return;
            }

            bindForward = glm::normalize(bindForward);
            bindDown = glm::normalize(bindDown);

            glm::vec3 bindRight = glm::cross(bindForward, bindDown);
            if (glm::length2(bindRight) < 1e-5f)
            {
                bindRight = glm::cross(bindForward, glm::vec3(0.0f, 1.0f, 0.0f));
            }
            if (glm::length2(bindRight) < 1e-5f)
            {
                return;
            }
            bindRight = glm::normalize(bindRight);

            glm::vec3 bindUp = glm::normalize(glm::cross(bindForward, bindRight));
            if (glm::dot(bindUp, glm::vec3(0.0f, 1.0f, 0.0f)) < 0.0f)
            {
                bindRight = -bindRight;
                bindUp = -bindUp;
            }

            const glm::quat footBindGlobalRotation = glm::normalize(glm::quat_cast(glm::mat3(bindGlobals[chain.foot])));
            const glm::quat inverseBindRotation = glm::inverse(footBindGlobalRotation);
            chain.localForwardAxis = glm::normalize(inverseBindRotation * bindForward);
            chain.localRightAxis = glm::normalize(inverseBindRotation * bindRight);
            chain.localUpAxis = glm::normalize(inverseBindRotation * bindUp);

            const glm::quat toeBindGlobalRotation = glm::normalize(glm::quat_cast(glm::mat3(bindGlobals[chain.toe])));
            const glm::quat inverseToeBindRotation = glm::inverse(toeBindGlobalRotation);
            chain.toeLocalForwardAxis = glm::normalize(inverseToeBindRotation * bindForward);
            chain.toeLocalRightAxis = glm::normalize(inverseToeBindRotation * bindRight);
            chain.toeLocalUpAxis = glm::normalize(inverseToeBindRotation * bindUp);
        };

        footPlacementChainsValid_ =
            hipsJointIndex_ != -1 &&
            leftFootPlacementChain_.upperLeg != -1 &&
            leftFootPlacementChain_.lowerLeg != -1 &&
            leftFootPlacementChain_.foot != -1 &&
            rightFootPlacementChain_.upperLeg != -1 &&
            rightFootPlacementChain_.lowerLeg != -1 &&
            rightFootPlacementChain_.foot != -1;

        if (!footPlacementChainsValid_)
        {
            SPDLOG_WARN("Foot placement IK disabled: failed to resolve required leg joints for skeleton '{}'", skeleton_.Name);
        }
        else
        {
            const std::vector<glm::mat4> bindGlobals = buildBindGlobals();
            initializeFootAxes(leftFootPlacementChain_, bindGlobals);
            initializeFootAxes(rightFootPlacementChain_, bindGlobals);
        }

        return footPlacementChainsValid_;
    }

    bool SkinnedMeshComponent::SampleGroundHeight(const glm::mat4& componentWorldTransform, int footJointIndex,
                                                  float& outFootOffset, glm::vec3& outGroundNormal) const
    {
        if (!owner_ || footJointIndex < 0 || footJointIndex >= static_cast<int>(runtimeJoints_.size()))
        {
            return false;
        }

        const glm::vec3 footWorldPos =
            glm::vec3(componentWorldTransform * runtimeJoints_[footJointIndex].GlobalTransform * glm::vec4(0, 0, 0, 1));
        const glm::vec3 traceOrigin = footWorldPos + glm::vec3(0.0f, footPlacementIKSettings_.TraceUpDistance, 0.0f);
        Assets::RayCastResult hit = NextEngine::GetInstance()->GetScene().GetCPUAccelerationStructure().RayCastInCPU(
            traceOrigin, glm::vec3(0.0f, -1.0f, 0.0f));

        if (!hit.Hitted)
        {
            return false;
        }

        const float maxTraceDistance =
            footPlacementIKSettings_.TraceUpDistance + footPlacementIKSettings_.TraceDownDistance;
        if (hit.T > maxTraceDistance || hit.Normal.y < footPlacementIKSettings_.MinGroundNormalY)
        {
            return false;
        }

        const float desiredFootY = hit.HitPoint.y + footPlacementIKSettings_.FootHeight;
        outFootOffset = glm::clamp(desiredFootY - footWorldPos.y,
                                   -footPlacementIKSettings_.MaxFootDrop,
                                   footPlacementIKSettings_.MaxFootLift);
        outGroundNormal = glm::normalize(glm::vec3(hit.Normal));
        return true;
    }

    // Geometric two-bone IK. We use a hand-rolled solver instead of ozz's IKTwoBoneJob
    // because ozz requires a per-skeleton mid_axis convention (in mid-joint local space)
    // that depends on rig orientation. Mannequin's bind pose is not a canonical T-pose
    // (foot is 46° toes-down), which makes runtime derivation of mid_axis fragile and
    // produced a 180° flip during testing. Until we add per-rig IK config, this geometric
    // solver — which only depends on current-pose geometry — is the robust choice.
    void SkinnedMeshComponent::SolveTwoBoneIK(int upperJointIndex, int lowerJointIndex, int endJointIndex,
                                              const glm::vec3& targetModelSpace)
    {
        if (upperJointIndex < 0 || lowerJointIndex < 0 || endJointIndex < 0)
        {
            return;
        }

        const glm::vec3 upperPos = glm::vec3(runtimeJoints_[upperJointIndex].GlobalTransform[3]);
        const glm::vec3 lowerPos = glm::vec3(runtimeJoints_[lowerJointIndex].GlobalTransform[3]);
        const glm::vec3 endPos = glm::vec3(runtimeJoints_[endJointIndex].GlobalTransform[3]);

        const float upperLength = glm::length(lowerPos - upperPos);
        const float lowerLength = glm::length(endPos - lowerPos);
        if (upperLength < 1e-4f || lowerLength < 1e-4f)
        {
            return;
        }

        glm::vec3 targetDir = targetModelSpace - upperPos;
        float targetDistance = glm::length(targetDir);
        if (targetDistance < 1e-4f)
        {
            return;
        }

        targetDir /= targetDistance;
        targetDistance = glm::clamp(targetDistance, 1e-4f, upperLength + lowerLength - 1e-4f);

        glm::vec3 bendNormal = glm::cross(lowerPos - upperPos, endPos - lowerPos);
        if (glm::length2(bendNormal) < 1e-5f)
        {
            bendNormal = glm::cross(endPos - upperPos, glm::vec3(0.0f, 1.0f, 0.0f));
            if (glm::length2(bendNormal) < 1e-5f)
            {
                bendNormal = glm::cross(endPos - upperPos, glm::vec3(1.0f, 0.0f, 0.0f));
            }
        }
        bendNormal = glm::normalize(bendNormal);

        glm::vec3 bendAxis = glm::cross(bendNormal, targetDir);
        if (glm::length2(bendAxis) < 1e-5f)
        {
            return;
        }
        bendAxis = glm::normalize(bendAxis);
        if (glm::dot(lowerPos - upperPos, bendAxis) < 0.0f)
        {
            bendAxis = -bendAxis;
        }

        const float cosRootAngle = glm::clamp(
            (targetDistance * targetDistance + upperLength * upperLength - lowerLength * lowerLength) /
                (2.0f * targetDistance * upperLength),
            -1.0f, 1.0f);
        const float sinRootAngle = std::sqrt(std::max(0.0f, 1.0f - cosRootAngle * cosRootAngle));
        const glm::vec3 desiredLowerPos =
            upperPos + targetDir * (cosRootAngle * upperLength) + bendAxis * (sinRootAngle * upperLength);

        const int upperParentIndex = skeleton_.Joints[upperJointIndex].ParentIndex;
        const glm::quat upperParentGlobalRotation =
            upperParentIndex >= 0 ? ExtractJointGlobalRotation(upperParentIndex) : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        const glm::quat upperGlobalRotation = ExtractJointGlobalRotation(upperJointIndex);
        const glm::quat upperDelta = MakeRotationBetween(lowerPos - upperPos, desiredLowerPos - upperPos);
        runtimeJoints_[upperJointIndex].Rotation =
            glm::normalize(glm::inverse(upperParentGlobalRotation) * upperDelta * upperGlobalRotation);

        UpdateJoints();

        const glm::vec3 updatedLowerPos = glm::vec3(runtimeJoints_[lowerJointIndex].GlobalTransform[3]);
        const glm::vec3 updatedEndPos = glm::vec3(runtimeJoints_[endJointIndex].GlobalTransform[3]);
        const glm::vec3 desiredEndDir = targetModelSpace - updatedLowerPos;
        if (glm::length2(desiredEndDir) < 1e-5f)
        {
            return;
        }

        const int lowerParentIndex = skeleton_.Joints[lowerJointIndex].ParentIndex;
        const glm::quat lowerParentGlobalRotation =
            lowerParentIndex >= 0 ? ExtractJointGlobalRotation(lowerParentIndex) : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        const glm::quat lowerGlobalRotation = ExtractJointGlobalRotation(lowerJointIndex);
        const glm::quat lowerDelta = MakeRotationBetween(updatedEndPos - updatedLowerPos, desiredEndDir);
        runtimeJoints_[lowerJointIndex].Rotation =
            glm::normalize(glm::inverse(lowerParentGlobalRotation) * lowerDelta * lowerGlobalRotation);
    }

    // Hand-rolled toe alignment. See note on SolveTwoBoneIK above — ozz::IKAimJob is
    // available but requires correct local-space axes for the rig, which Mannequin's
    // non-canonical bind pose makes fragile to derive automatically.
    void SkinnedMeshComponent::AlignToeToGround(const FootPlacementChain& chain, const glm::mat4& componentWorldTransform)
    {
        if (chain.toe == -1 || !chain.toeHitValid)
        {
            return;
        }

        const glm::mat3 worldToModelNormalMatrix = glm::transpose(glm::inverse(glm::mat3(componentWorldTransform)));
        glm::vec3 desiredUp = worldToModelNormalMatrix * chain.toeGroundNormal;
        if (glm::length2(desiredUp) < 1e-5f)
        {
            return;
        }
        desiredUp = glm::normalize(desiredUp);

        const glm::quat toeGlobalRotation = ExtractJointGlobalRotation(chain.toe);
        const glm::vec3 currentNormalAxis = glm::normalize(toeGlobalRotation * glm::vec3(0.0f, 0.0f, 1.0f));
        const glm::vec3 currentForwardAxis = glm::normalize(toeGlobalRotation * glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::vec3 currentSideAxis = glm::normalize(toeGlobalRotation * glm::vec3(1.0f, 0.0f, 0.0f));
        if (glm::length2(currentNormalAxis) < 1e-5f ||
            glm::length2(currentForwardAxis) < 1e-5f ||
            glm::length2(currentSideAxis) < 1e-5f)
        {
            return;
        }

        const glm::quat alignUpDelta = MakeRotationBetween(currentNormalAxis, desiredUp);

        glm::vec3 alignedForward = ProjectOntoPlane(alignUpDelta * currentForwardAxis, desiredUp);
        glm::vec3 desiredForward = ProjectOntoPlane(currentForwardAxis, desiredUp);
        if (glm::length2(desiredForward) < 1e-5f)
        {
            desiredForward = glm::cross(desiredUp, currentSideAxis);
        }
        if (glm::length2(alignedForward) < 1e-5f)
        {
            alignedForward = desiredForward;
        }
        if (glm::length2(desiredForward) < 1e-5f || glm::length2(alignedForward) < 1e-5f)
        {
            return;
        }

        desiredForward = glm::normalize(desiredForward);
        alignedForward = glm::normalize(alignedForward);

        const float twistAngle = SignedAngleAroundAxis(alignedForward, desiredForward, desiredUp);
        const glm::quat twistDelta = glm::angleAxis(twistAngle, desiredUp);
        const glm::quat targetGlobalRotation = glm::normalize(twistDelta * alignUpDelta * toeGlobalRotation);

        const int toeParentIndex = skeleton_.Joints[chain.toe].ParentIndex;
        const glm::quat toeParentGlobalRotation =
            toeParentIndex >= 0 ? ExtractJointGlobalRotation(toeParentIndex) : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        runtimeJoints_[chain.toe].Rotation =
            glm::normalize(glm::inverse(toeParentGlobalRotation) * targetGlobalRotation);
    }

    void SkinnedMeshComponent::DrawFootPlacementDebug(const FootPlacementChain& chain,
                                                     const glm::mat4& componentWorldTransform) const
    {
        constexpr float axisLength = 0.18f;
        constexpr float footAxisLength = 0.14f;
        constexpr float toeAxisLength = 0.12f;

        const glm::vec3 footDebugNormal = glm::normalize(chain.groundNormal);
        const glm::vec3 toeDebugNormal = chain.toeHitValid ? glm::normalize(chain.toeGroundNormal) : footDebugNormal;

        if (chain.footHitValid)
        {
            Runtime::EngineHelper::DrawAuxPoint(chain.footHitPoint, glm::vec4(1.0f, 0.5f, 0.1f, 1.0f), 5.0f);
            Runtime::EngineHelper::DrawAuxLine(chain.footHitPoint, chain.footHitPoint + footDebugNormal * axisLength,
                                          glm::vec4(1.0f, 0.7f, 0.1f, 1.0f), 2.0f);
        }

        if (chain.toeHitValid)
        {
            Runtime::EngineHelper::DrawAuxPoint(chain.toeHitPoint, glm::vec4(1.0f, 0.1f, 0.8f, 1.0f), 5.0f);
            Runtime::EngineHelper::DrawAuxLine(chain.toeHitPoint, chain.toeHitPoint + toeDebugNormal * axisLength,
                                          glm::vec4(1.0f, 0.2f, 0.9f, 1.0f), 2.0f);
        }

        if (chain.foot != -1)
        {
            const glm::vec3 footWorldPos =
                glm::vec3(componentWorldTransform * runtimeJoints_[chain.foot].GlobalTransform * glm::vec4(0, 0, 0, 1));
            const glm::quat footGlobalRotation = ExtractJointGlobalRotation(chain.foot);
            const glm::vec3 footRightAxisModel = glm::normalize(footGlobalRotation * glm::vec3(1.0f, 0.0f, 0.0f));
            const glm::vec3 footUpAxisModel = glm::normalize(footGlobalRotation * glm::vec3(0.0f, 1.0f, 0.0f));
            const glm::vec3 footForwardAxisModel = glm::normalize(footGlobalRotation * glm::vec3(0.0f, 0.0f, 1.0f));
            const glm::vec3 footRightAxis =
                glm::normalize(glm::vec3(componentWorldTransform * glm::vec4(footRightAxisModel, 0.0f)));
            const glm::vec3 footUpAxis =
                glm::normalize(glm::vec3(componentWorldTransform * glm::vec4(footUpAxisModel, 0.0f)));
            const glm::vec3 footForwardAxis =
                glm::normalize(glm::vec3(componentWorldTransform * glm::vec4(footForwardAxisModel, 0.0f)));

            Runtime::EngineHelper::DrawAuxLine(footWorldPos, footWorldPos + footRightAxis * footAxisLength,
                                          glm::vec4(0.75f, 0.0f, 0.0f, 1.0f), 2.0f);
            Runtime::EngineHelper::DrawAuxLine(footWorldPos, footWorldPos + footUpAxis * footAxisLength,
                                          glm::vec4(0.0f, 0.75f, 0.0f, 1.0f), 2.0f);
            Runtime::EngineHelper::DrawAuxLine(footWorldPos, footWorldPos + footForwardAxis * footAxisLength,
                                          glm::vec4(0.0f, 0.35f, 0.75f, 1.0f), 2.0f);
        }

        if (chain.toe != -1)
        {
            const glm::vec3 toeWorldPos =
                glm::vec3(componentWorldTransform * runtimeJoints_[chain.toe].GlobalTransform * glm::vec4(0, 0, 0, 1));
            const glm::quat toeGlobalRotation = ExtractJointGlobalRotation(chain.toe);
            const glm::vec3 toeRightAxisModel = glm::normalize(toeGlobalRotation * glm::vec3(1.0f, 0.0f, 0.0f));
            const glm::vec3 toeUpAxisModel = glm::normalize(toeGlobalRotation * glm::vec3(0.0f, 1.0f, 0.0f));
            const glm::vec3 toeForwardAxisModel = glm::normalize(toeGlobalRotation * glm::vec3(0.0f, 0.0f, 1.0f));
            const glm::vec3 toeRightAxis =
                glm::normalize(glm::vec3(componentWorldTransform * glm::vec4(toeRightAxisModel, 0.0f)));
            const glm::vec3 toeUpAxis =
                glm::normalize(glm::vec3(componentWorldTransform * glm::vec4(toeUpAxisModel, 0.0f)));
            const glm::vec3 toeForwardAxis =
                glm::normalize(glm::vec3(componentWorldTransform * glm::vec4(toeForwardAxisModel, 0.0f)));

            Runtime::EngineHelper::DrawAuxLine(toeWorldPos, toeWorldPos + toeRightAxis * toeAxisLength,
                                          glm::vec4(1.0f, 0.15f, 0.15f, 1.0f), 2.0f);
            Runtime::EngineHelper::DrawAuxLine(toeWorldPos, toeWorldPos + toeUpAxis * toeAxisLength,
                                          glm::vec4(0.15f, 1.0f, 0.15f, 1.0f), 2.0f);
            Runtime::EngineHelper::DrawAuxLine(toeWorldPos, toeWorldPos + toeForwardAxis * toeAxisLength,
                                          glm::vec4(0.2f, 0.55f, 1.0f, 1.0f), 2.0f);
        }
    }

    void SkinnedMeshComponent::ApplyFootPlacementIK(float deltaTime)
    {
        if (!owner_ || !ResolveFootPlacementChains())
        {
            return;
        }

        const float targetBlendWeight = footPlacementIKSettings_.Enabled ? footPlacementIKSettings_.Weight : 0.0f;
        const float blendSharpness =
            targetBlendWeight > footPlacementBlendWeight_ ? footPlacementIKSettings_.BlendInSpeed : footPlacementIKSettings_.BlendOutSpeed;
        footPlacementBlendWeight_ = DampToward(footPlacementBlendWeight_, targetBlendWeight, blendSharpness, deltaTime);
        const glm::mat4 componentWorldTransform = owner_->WorldTransform();

        if (footPlacementBlendWeight_ < 0.001f)
        {
            leftFootPlacementChain_.currentOffset = 0.0f;
            rightFootPlacementChain_.currentOffset = 0.0f;
            pelvisOffset_ = 0.0f;
            if (footPlacementIKSettings_.DebugDraw)
            {
                DrawFootPlacementDebug(leftFootPlacementChain_, componentWorldTransform);
                DrawFootPlacementDebug(rightFootPlacementChain_, componentWorldTransform);
            }
            return;
        }

        auto updateChainOffset = [this, &componentWorldTransform, deltaTime](FootPlacementChain& chain)
        {
            float footOffset = 0.0f;
            glm::vec3 footNormal(0.0f, 1.0f, 0.0f);
            const bool hasFootHit = SampleGroundHeight(componentWorldTransform, chain.foot, footOffset, footNormal);
            chain.footHitValid = false;
            if (hasFootHit)
            {
                const glm::vec3 footWorldPos =
                    glm::vec3(componentWorldTransform * runtimeJoints_[chain.foot].GlobalTransform * glm::vec4(0, 0, 0, 1));
                chain.footHitPoint = footWorldPos + glm::vec3(0.0f, footOffset - footPlacementIKSettings_.FootHeight, 0.0f);
                chain.footHitValid = true;
            }

            float toeOffset = footOffset;
            glm::vec3 toeNormal = footNormal;
            const bool hasToeHit =
                chain.toe != -1 && SampleGroundHeight(componentWorldTransform, chain.toe, toeOffset, toeNormal);
            chain.toeHitValid = false;
            chain.toeGroundNormal = glm::vec3(0.0f, 1.0f, 0.0f);
            if (hasToeHit)
            {
                const glm::vec3 toeWorldPos =
                    glm::vec3(componentWorldTransform * runtimeJoints_[chain.toe].GlobalTransform * glm::vec4(0, 0, 0, 1));
                chain.toeHitPoint = toeWorldPos + glm::vec3(0.0f, toeOffset - footPlacementIKSettings_.FootHeight, 0.0f);
                chain.toeHitValid = true;
                chain.toeGroundNormal = toeNormal;
            }

            float targetOffset = 0.0f;
            glm::vec3 groundNormal(0.0f, 1.0f, 0.0f);
            if (hasFootHit || hasToeHit)
            {
                if (hasFootHit && hasToeHit)
                {
                    targetOffset = std::max(footOffset, toeOffset);
                    groundNormal = glm::normalize(footNormal + toeNormal);
                }
                else if (hasToeHit)
                {
                    targetOffset = toeOffset;
                    groundNormal = toeNormal;
                }
                else
                {
                    targetOffset = footOffset;
                    groundNormal = footNormal;
                }

                chain.groundNormal = glm::normalize(groundNormal);
            }
            else
            {
                targetOffset = 0.0f;
                chain.groundNormal = glm::vec3(0.0f, 1.0f, 0.0f);
            }

            targetOffset *= footPlacementBlendWeight_;
            chain.currentOffset = DampToward(chain.currentOffset, targetOffset,
                                             footPlacementIKSettings_.FootOffsetSharpness, deltaTime);
        };

        updateChainOffset(leftFootPlacementChain_);
        updateChainOffset(rightFootPlacementChain_);

        const float targetPelvisOffset = glm::clamp(
            std::min({0.0f, leftFootPlacementChain_.currentOffset, rightFootPlacementChain_.currentOffset}) *
                footPlacementIKSettings_.PelvisWeight,
            -footPlacementIKSettings_.PelvisMaxOffset,
            footPlacementIKSettings_.PelvisMaxOffset);
        pelvisOffset_ = DampToward(pelvisOffset_, targetPelvisOffset, footPlacementIKSettings_.PelvisSharpness, deltaTime);

        runtimeJoints_[hipsJointIndex_].Translation.y += pelvisOffset_;
        UpdateJoints();

        const glm::mat4 componentWorldInverse = glm::inverse(componentWorldTransform);

        auto solveChain = [this, &componentWorldTransform, &componentWorldInverse](const FootPlacementChain& chain)
        {
            const float remainingOffset = chain.currentOffset - pelvisOffset_;
            if (std::abs(remainingOffset) >= 0.001f)
            {
                const glm::vec3 footWorldPos =
                    glm::vec3(componentWorldTransform * runtimeJoints_[chain.foot].GlobalTransform * glm::vec4(0, 0, 0, 1));
                const glm::vec3 targetWorldPos = footWorldPos + glm::vec3(0.0f, remainingOffset, 0.0f);
                const glm::vec3 targetModelPos = glm::vec3(componentWorldInverse * glm::vec4(targetWorldPos, 1.0f));
                SolveTwoBoneIK(chain.upperLeg, chain.lowerLeg, chain.foot, targetModelPos);
                UpdateJoints();
            }

            if (chain.toeHitValid)
            {
                AlignToeToGround(chain, componentWorldTransform);
                UpdateJoints();
            }

        };

        solveChain(leftFootPlacementChain_);
        solveChain(rightFootPlacementChain_);

        if (footPlacementIKSettings_.DebugDraw)
        {
            DrawFootPlacementDebug(leftFootPlacementChain_, componentWorldTransform);
            DrawFootPlacementDebug(rightFootPlacementChain_, componentWorldTransform);
        }
    }
}
