#pragma once
#include "Common/CoreMinimal.hpp"
#include "Assets/Data/Skeleton.hpp"
#include "Assets/Data/Animation.hpp"
#include "Assets/Core/Model.hpp"
#include "Assets/Core/Component.h"
#include "Runtime/Reflection/ReflectionMacros.h"
#include <map>
#include <vector>
#include <string>

namespace Runtime
{
    class SkinnedMeshComponent : public Assets::Component
    {
    public:
        REFLECT_COMPONENT(SkinnedMeshComponent)

        struct FootPlacementIKSettings
        {
            bool Enabled = false;
            float Weight = 1.0f;
            float BlendInSpeed = 10.0f;
            float BlendOutSpeed = 14.0f;
            float TraceUpDistance = 0.45f;
            float TraceDownDistance = 0.85f;
            float FootHeight = 0.03f;
            float MaxFootLift = 0.30f;
            float MaxFootDrop = 0.35f;
            float MaxFootPitchDegrees = 18.0f;
            float MaxFootRollDegrees = 14.0f;
            float PelvisWeight = 0.75f;
            float PelvisMaxOffset = 0.25f;
            float FootOffsetSharpness = 14.0f;
            float PelvisSharpness = 10.0f;
            float MinGroundNormalY = 0.35f;
            bool DebugDraw = false;
        };
        
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
        void SetFootPlacementIKSettings(const FootPlacementIKSettings& settings) { footPlacementIKSettings_ = settings; }
        const FootPlacementIKSettings& GetFootPlacementIKSettings() const { return footPlacementIKSettings_; }
        void SetFootPlacementIKEnabled(bool enabled) { footPlacementIKSettings_.Enabled = enabled; }
        void SetFootPlacementIKWeight(float weight) { footPlacementIKSettings_.Weight = weight; }
        
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
        std::vector<RuntimeJoint> blendSourceJoints_;
        bool blendActive_ = false;
        float blendElapsed_ = 0.0f;
        float blendDuration_ = 0.12f;
        void ResetJointsToBindPose(std::vector<RuntimeJoint>& joints) const;
        void EvaluateAnimationState(const AnimationState& state, std::vector<RuntimeJoint>& joints) const;
        void AdvanceAnimationState(AnimationState& state, float deltaTime) const;
        void ApplyFootPlacementIK(float deltaTime);
        int FindJointIndex(std::initializer_list<std::string_view> aliases,
                           std::initializer_list<std::string_view> containsTokens = {}) const;
        glm::quat ExtractJointGlobalRotation(int jointIndex) const;
        glm::quat MakeRotationBetween(const glm::vec3& from, const glm::vec3& to) const;
        bool ResolveFootPlacementChains();
        bool SampleGroundHeight(const glm::mat4& componentWorldTransform, int footJointIndex,
                                float& outFootOffset, glm::vec3& outGroundNormal) const;
        void SolveTwoBoneIK(int upperJointIndex, int lowerJointIndex, int endJointIndex, const glm::vec3& targetModelSpace);

        struct FootPlacementChain
        {
            int upperLeg = -1;
            int lowerLeg = -1;
            int foot = -1;
            int toe = -1;
            float currentOffset = 0.0f;
            glm::vec3 groundNormal = glm::vec3(0.0f, 1.0f, 0.0f);
            glm::vec3 localForwardAxis = glm::vec3(0.0f, 0.0f, 1.0f);
            glm::vec3 localRightAxis = glm::vec3(1.0f, 0.0f, 0.0f);
            glm::vec3 localUpAxis = glm::vec3(0.0f, 1.0f, 0.0f);
            glm::vec3 toeLocalForwardAxis = glm::vec3(0.0f, 0.0f, 1.0f);
            glm::vec3 toeLocalRightAxis = glm::vec3(1.0f, 0.0f, 0.0f);
            glm::vec3 toeLocalUpAxis = glm::vec3(0.0f, 1.0f, 0.0f);
            bool footHitValid = false;
            bool toeHitValid = false;
            glm::vec3 footHitPoint = glm::vec3(0.0f);
            glm::vec3 toeHitPoint = glm::vec3(0.0f);
            glm::vec3 toeGroundNormal = glm::vec3(0.0f, 1.0f, 0.0f);
        };
        void DrawFootPlacementDebug(const FootPlacementChain& chain, const glm::mat4& componentWorldTransform) const;
        void AlignToeToGround(const FootPlacementChain& chain, const glm::mat4& componentWorldTransform);

        Assets::Skeleton skeleton_;
        std::map<std::string, int> jointMap_;
        FootPlacementIKSettings footPlacementIKSettings_;
        FootPlacementChain leftFootPlacementChain_;
        FootPlacementChain rightFootPlacementChain_;
        int hipsJointIndex_ = -1;
        bool footPlacementChainsResolved_ = false;
        bool footPlacementChainsValid_ = false;
        float footPlacementBlendWeight_ = 0.0f;
        float pelvisOffset_ = 0.0f;
    };
}
