#include "SkinnedMeshComponent.h"
#include <algorithm>
#include <spdlog/spdlog.h>
#include <functional>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>

namespace Runtime
{
    SkinnedMeshComponent::SkinnedMeshComponent(const Assets::Skeleton& skeleton)
        : skeleton_(skeleton)
    {
        runtimeJoints_.resize(skeleton_.Joints.size());
        jointMatrices_.resize(skeleton_.Joints.size(), glm::mat4(1.0f));
        
        for (size_t i = 0; i < skeleton_.Joints.size(); ++i)
        {
            runtimeJoints_[i].Translation = skeleton_.Joints[i].Translation;
            runtimeJoints_[i].Rotation = skeleton_.Joints[i].Rotation;
            runtimeJoints_[i].Scale = skeleton_.Joints[i].Scale;
        }
        
        UpdateJoints();
        
        currentState_.Playing = false;
        currentState_.CurrentTime = 0.0f;
    }

    void SkinnedMeshComponent::AddAnimations(const std::vector<Assets::AnimationTrack>& allTracks)
    {
        std::map<std::string, int> jointMap;
        for(size_t i=0; i<skeleton_.Joints.size(); ++i)
        {
            jointMap[skeleton_.Joints[i].Name] = static_cast<int>(i);
        }
        
        for (const auto& track : allTracks)
        {
            if (jointMap.find(track.NodeName_) != jointMap.end())
            {
                animations_[track.AnimationName].push_back(track);
            }
        }
    }

    void SkinnedMeshComponent::PlayAnimation(const std::string& name, bool loop)
    {
        if (animations_.find(name) == animations_.end())
        {
            SPDLOG_WARN("Animation '{}' not found", name);
            return;
        }
        
        currentState_.Name = name;
        currentState_.Loop = loop;
        currentState_.CurrentTime = 0.0f;
        currentState_.Playing = true;
        
        currentState_.Duration = 0.0f;
        for (const auto& track : animations_[name])
        {
            currentState_.Duration = std::max(currentState_.Duration, track.Duration_);
        }
    }

    void SkinnedMeshComponent::StopAnimation()
    {
        currentState_.Playing = false;
    }

    void SkinnedMeshComponent::Update(float deltaTime)
    {
        if (!currentState_.Playing) return;
        
        currentState_.CurrentTime += deltaTime;
        
        if (currentState_.CurrentTime > currentState_.Duration)
        {
            if (currentState_.Loop)
            {
                currentState_.CurrentTime = fmod(currentState_.CurrentTime, currentState_.Duration);
            }
            else
            {
                currentState_.CurrentTime = currentState_.Duration;
                currentState_.Playing = false;
            }
        }
        
        const auto& tracks = animations_[currentState_.Name];
        
        std::map<std::string, int> jointMap;
        for(size_t i=0; i<skeleton_.Joints.size(); ++i)
        {
            jointMap[skeleton_.Joints[i].Name] = static_cast<int>(i);
        }

        for (const auto& track : tracks)
        {
            if (jointMap.find(track.NodeName_) == jointMap.end()) continue;
            
            int jointIdx = jointMap[track.NodeName_];
            auto& joint = runtimeJoints_[jointIdx];
            
            glm::vec3 t = joint.Translation;
            glm::quat r = joint.Rotation;
            glm::vec3 s = joint.Scale;
            
            const_cast<Assets::AnimationTrack&>(track).Sample(currentState_.CurrentTime, t, r, s);
            
            joint.Translation = t;
            joint.Rotation = r;
            joint.Scale = s;
        }
        
        UpdateJoints();
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
}
