#pragma once
#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Assets/Data/Skeleton.hpp"
#include "Engine/Assets/Core/Model.hpp"
#include "Engine/Assets/Core/Component.hpp"
#include "Engine/Runtime/Reflection/ReflectionMacros.hpp"

namespace Runtime
{
    struct SkinnedMeshOzzState;

    class SkinnedMeshComponent : public Assets::Component
    {
    public:
        REFLECT_COMPONENT(SkinnedMeshComponent)

        SkinnedMeshComponent(const Assets::Skeleton& skeleton);
        ~SkinnedMeshComponent();

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
        bool IsPlaying() const { return currentState_.Playing; }
        bool GetIsPlaying() const { return currentState_.Playing; }

    private:
        void UpdateJoints();
        
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
        AnimationState blendSourceState_;
        bool blendActive_ = false;
        float blendElapsed_ = 0.0f;
        float blendDuration_ = 0.12f;
        void AdvanceAnimationState(AnimationState& state, float deltaTime) const;
        // Samples one named animation at state.CurrentTime into the supplied locals buffer.
        // Returns false if no ozz animation matches the name (locals untouched).
        bool SampleOzz(const AnimationState& state, int contextSlot);
        // Blends the two sampled local buffers (alpha = current weight in [0,1]) into the
        // primary buffer, runs LocalToModel, and mirrors the result into runtimeJoints_/jointMatrices_.
        void FinalizePose(float currentWeight);
        Assets::Skeleton skeleton_;
        std::map<std::string, int> jointMap_;
        std::unique_ptr<SkinnedMeshOzzState> ozz_;
    };
}
