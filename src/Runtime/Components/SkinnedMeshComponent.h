#pragma once
#include "Common/CoreMinimal.hpp"
#include "Assets/Skeleton.hpp"
#include "Assets/Animation.hpp"
#include "Assets/Model.hpp"
#include "Assets/Component.h"
#include <map>
#include <vector>
#include <string>

namespace Runtime
{
    class SkinnedMeshComponent : public Assets::Component
    {
    public:
        SkinnedMeshComponent(const Assets::Skeleton& skeleton);
        
        void Update(float deltaTime);
        
        // Add all tracks relevant to this skeleton from a global list
        void AddAnimations(const std::vector<Assets::AnimationTrack>& allTracks);
        
        void PlayAnimation(const std::string& name, bool loop = true);
        void StopAnimation();
        void SetPlaySpeed(float speed) { currentState_.PlaySpeed = speed; }
        float GetPlaySpeed() const { return currentState_.PlaySpeed; }
        
        void DrawDebugSkeleton(const glm::mat4& worldTransform);

        const std::vector<glm::mat4>& GetJointMatrices() const { return jointMatrices_; }
        const Assets::Skeleton& GetSkeleton() const { return skeleton_; }
        std::vector<std::string> GetAnimationNames() const;
        std::string GetCurrentAnimationName() const { return currentState_.Playing ? currentState_.Name : ""; }

    private:
        void UpdateJoints();

        Assets::Skeleton skeleton_;
        
        struct RuntimeJoint
        {
            glm::vec3 Translation;
            glm::quat Rotation;
            glm::vec3 Scale;
            glm::mat4 GlobalTransform;
        };
        std::vector<RuntimeJoint> runtimeJoints_;
        std::vector<glm::mat4> jointMatrices_; 
        
        struct AnimationState
        {
            std::string Name;
            float CurrentTime;
            float Duration;
            float PlaySpeed = 1.0f;
            bool Loop;
            bool Playing;
        };
        
        // Tracks grouped by Animation Name
        // Map: AnimationName -> List of tracks targeting joints in this skeleton
        std::map<std::string, std::vector<Assets::AnimationTrack>> animations_;
        AnimationState currentState_;
    };
}