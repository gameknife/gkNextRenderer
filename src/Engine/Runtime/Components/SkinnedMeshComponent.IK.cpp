// SkinnedMeshComponent procedural IK: foot placement chains, two-bone IK,
// toe alignment and ground sampling.
// Split from SkinnedMeshComponent.cpp; same class, separate TU.
#include "Engine/Runtime/Components/SkinnedMeshComponent.h"
#include "Engine/Runtime/Components/SkinnedMeshComponent.Internal.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Utilities/NextEngineHelper.h"
#include "Engine/Assets/Core/Node.h"

namespace Runtime
{
    namespace
    {
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
