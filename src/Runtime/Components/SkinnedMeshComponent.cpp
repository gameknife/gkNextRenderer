#include "SkinnedMeshComponent.h"
#include "Runtime/Engine.hpp"
#include "Runtime/Utilities/NextEngineHelper.h"
#include "Runtime/Reflection/PropertyMeta.h"
#include <algorithm>
#include <spdlog/spdlog.h>
#include <functional>
#include <entt/meta/factory.hpp>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>

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
    {
        runtimeJoints_.resize(skeleton_.Joints.size());
        blendSourceJoints_.resize(skeleton_.Joints.size());
        jointMatrices_.resize(skeleton_.Joints.size(), glm::mat4(1.0f));
        
        for (size_t i = 0; i < skeleton_.Joints.size(); ++i)
        {
            jointMap_[skeleton_.Joints[i].Name] = static_cast<int>(i);
            runtimeJoints_[i].Translation = skeleton_.Joints[i].Translation;
            runtimeJoints_[i].Rotation = skeleton_.Joints[i].Rotation;
            runtimeJoints_[i].Scale = skeleton_.Joints[i].Scale;
            blendSourceJoints_[i] = runtimeJoints_[i];
        }
        
        UpdateJoints();
        
        currentState_.Playing = false;
        currentState_.CurrentTime = 0.0f;
        blendSourceState_.Playing = false;
        blendSourceState_.CurrentTime = 0.0f;
    }

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
            EvaluateAnimationState(blendSourceState_, blendSourceJoints_);
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
        EvaluateAnimationState(currentState_, runtimeJoints_);

        if (blendActive_)
        {
            AdvanceAnimationState(blendSourceState_, deltaTime);
            EvaluateAnimationState(blendSourceState_, blendSourceJoints_);

            blendElapsed_ += deltaTime;
            const float blendAlpha = glm::clamp(blendElapsed_ / blendDuration_, 0.0f, 1.0f);

            for (size_t i = 0; i < runtimeJoints_.size(); ++i)
            {
                runtimeJoints_[i].Translation =
                    glm::mix(blendSourceJoints_[i].Translation, runtimeJoints_[i].Translation, blendAlpha);
                runtimeJoints_[i].Rotation =
                    glm::slerp(blendSourceJoints_[i].Rotation, runtimeJoints_[i].Rotation, blendAlpha);
                runtimeJoints_[i].Scale =
                    glm::mix(blendSourceJoints_[i].Scale, runtimeJoints_[i].Scale, blendAlpha);
            }

            if (blendAlpha >= 1.0f)
            {
                blendActive_ = false;
            }
        }

        UpdateJoints();
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
                
                NextEngineHelper::DrawAuxLine(start, end, glm::vec4(0, 1, 0, 1), 2.0f);
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

    void SkinnedMeshComponent::ResetJointsToBindPose(std::vector<RuntimeJoint>& joints) const
    {
        joints.resize(skeleton_.Joints.size());
        for (size_t i = 0; i < skeleton_.Joints.size(); ++i)
        {
            joints[i].Translation = skeleton_.Joints[i].Translation;
            joints[i].Rotation = skeleton_.Joints[i].Rotation;
            joints[i].Scale = skeleton_.Joints[i].Scale;
            joints[i].GlobalTransform = glm::mat4(1.0f);
        }
    }

    void SkinnedMeshComponent::EvaluateAnimationState(const AnimationState& state, std::vector<RuntimeJoint>& joints) const
    {
        ResetJointsToBindPose(joints);

        auto animIt = animations_.find(state.Name);
        if (animIt == animations_.end())
        {
            return;
        }

        for (const auto& track : animIt->second)
        {
            auto jointIt = jointMap_.find(track.NodeName_);
            if (jointIt == jointMap_.end())
            {
                continue;
            }

            RuntimeJoint& joint = joints[jointIt->second];
            glm::vec3 translation = joint.Translation;
            glm::quat rotation = joint.Rotation;
            glm::vec3 scale = joint.Scale;

            const_cast<Assets::AnimationTrack&>(track).Sample(state.CurrentTime, translation, rotation, scale);

            joint.Translation = translation;
            joint.Rotation = rotation;
            joint.Scale = scale;
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
