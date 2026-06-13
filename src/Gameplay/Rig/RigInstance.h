#pragma once

// ============================================================================
// RigInstance.h - Runtime side of the rigid-body rig (see RigAsset.hpp and
// docs/ScadRig-Design.md): FRigInstance builds one Node tree per character
// (bone nodes carry bind TRS, part render nodes hang under their bone), and
// FRigAnimator plays clips by writing bone-local TRS offsets on top of the
// bind pose. Animators hold raw Node pointers and never look nodes up by
// name, so many instances of the same rig can coexist in a scene.
// ============================================================================

#include "Engine/Assets/AssetsFwd.hpp"
#include "Engine/Assets/Data/RigAsset.hpp"
#include "Engine/Common/CoreMinimal.hpp"

#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace NextGameplay
{
    struct FRigInstanceDesc
    {
        std::string namePrefix;                                 // e.g. "agent_03"
        std::vector<uint32_t> partModelIds;                     // injected global model ids, aligned with asset.parts
        std::vector<std::array<uint32_t, 16>> partMaterialIds;  // per-part section -> material (tint already applied)
    };

    class FRigInstance
    {
    public:
        // Builds a Node tree for the rig and adds every node to the scene:
        // one node per bone (bind-pose TRS), plus one render node per part
        // parented to its bone. Returns the root bone node; outBoneNodes is
        // aligned with asset.bones.
        static std::shared_ptr<Assets::Node> Instantiate(
            Assets::Scene& scene,
            const Assets::FRigAsset& asset,
            const FRigInstanceDesc& desc,
            std::vector<Assets::Node*>& outBoneNodes);
    };

    class FRigAnimator
    {
    public:
        void Bind(const Assets::FRigAsset* asset, std::vector<Assets::Node*> boneNodes, Assets::Node* root);

        // Starts a clip with a linear crossfade from the previous one.
        // Re-playing the clip that is already current is a no-op.
        void Play(std::string_view clip, float fadeSeconds = 0.15f);
        void SetPlaySpeed(float speed) { playSpeed_ = speed; }
        // Per-instance de-synchronization: offsets the clip-local sample time.
        void SetPhaseOffset(float seconds) { phaseOffset_ = seconds; }
        const std::string& CurrentClip() const { return currentName_; }

        // Samples the active clip(s), writes bone TRS through the mutable
        // accessors, then refreshes the subtree with one RecalcTransform.
        void Update(float deltaSeconds);

    private:
        struct FBonePose
        {
            glm::vec3 t{0.0f};
            glm::quat r{1.0f, 0.0f, 0.0f, 0.0f};
            glm::vec3 s{1.0f};
        };

        struct FPlayState
        {
            const Assets::FRigClip* clip = nullptr;
            float time = 0.0f;
        };

        void Advance(FPlayState& state, float deltaSeconds) const;
        void SamplePose(const FPlayState& state, std::vector<FBonePose>& poses) const;
        void ApplyPose(const std::vector<FBonePose>& poses);

        const Assets::FRigAsset* asset_ = nullptr;
        std::vector<Assets::Node*> boneNodes_;   // aligned with asset_->bones
        Assets::Node* root_ = nullptr;

        FPlayState current_;
        FPlayState previous_;
        std::string currentName_;
        float playSpeed_ = 1.0f;
        float phaseOffset_ = 0.0f;
        float fadeDuration_ = 0.0f;
        float fadeRemaining_ = 0.0f;

        std::vector<FBonePose> scratchCurrent_;
        std::vector<FBonePose> scratchPrevious_;
    };
} // namespace NextGameplay
