#pragma once

// ============================================================================
// RigLayeredAnimator.h - Opt-in layered pose evaluation for rigid-body rigs.
//
// Existing simple consumers keep using FRigAnimator. Advanced characters can
// combine synchronized clip blends, masked override layers and additive
// one-shots without changing the FRigAsset / SCAD clip format.
// ============================================================================

#include "Engine/Assets/AssetsFwd.hpp"
#include "Engine/Assets/Data/RigAsset.hpp"

#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace NextGameplay
{
    enum class ERigPoseComponent : uint8_t
    {
        Position = 1 << 0,
        Rotation = 1 << 1,
        Scale = 1 << 2,
    };

    struct FRigBonePose
    {
        glm::vec3 t{0.0f};
        glm::quat r{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 s{1.0f};
        uint8_t authoredComponents = 0;

        bool Has(ERigPoseComponent component) const
        {
            return (authoredComponents & static_cast<uint8_t>(component)) != 0;
        }
        void Mark(ERigPoseComponent component)
        {
            authoredComponents |= static_cast<uint8_t>(component);
        }
    };

    struct FRigPose
    {
        std::vector<FRigBonePose> bones;

        void Reset(size_t boneCount);
    };

    struct FRigBoneMask
    {
        std::vector<float> weights;

        static FRigBoneMask FullBody(const Assets::FRigAsset& asset);
        static FRigBoneMask FromSubtree(const Assets::FRigAsset& asset, std::string_view rootBone,
                                        float weight = 1.0f);

        bool SetBoneWeight(const Assets::FRigAsset& asset, std::string_view boneName, float weight);
        float Weight(size_t boneIndex) const;
    };

    enum class ERigLayerBlendMode
    {
        Override,
        Additive,
    };

    struct FRigClipBlendSample
    {
        const Assets::FRigClip* clip = nullptr;
        float weight = 0.0f;
    };

    using FRigLayerHandle = uint32_t;
    inline constexpr FRigLayerHandle invalidRigLayerHandle = std::numeric_limits<FRigLayerHandle>::max();

    class FRigLayeredAnimator
    {
    public:
        void Bind(const Assets::FRigAsset* asset, std::vector<Assets::Node*> boneNodes, Assets::Node* root);

        FRigLayerHandle CreateLayer(std::string name, ERigLayerBlendMode blendMode, FRigBoneMask mask);
        bool SetLayerWeight(FRigLayerHandle layer, float weight, float fadeSeconds = 0.0f);

        // Loop samples share a normalized phase. Keeping the same syncGroup
        // across clip-set changes preserves foot phase.
        bool SetLoopBlend(FRigLayerHandle layer, std::vector<FRigClipBlendSample> samples,
                          std::string syncGroup, float playRate, float fadeSeconds = 0.15f);
        bool SetStaticBlend(FRigLayerHandle layer, std::vector<FRigClipBlendSample> samples,
                            float fadeSeconds = 0.15f);
        bool SetManualBlend(FRigLayerHandle layer, std::vector<FRigClipBlendSample> samples,
                            float normalizedTime, float fadeSeconds = 0.0f);
        bool PlayOneShot(FRigLayerHandle layer, const Assets::FRigClip* clip, float playRate = 1.0f,
                         float fadeInSeconds = 0.0f, float fadeOutSeconds = 0.05f,
                         bool restartIfSame = true);

        bool IsOneShotComplete(FRigLayerHandle layer) const;
        const Assets::FRigClip* DominantClip(FRigLayerHandle layer) const;
        bool IsBound() const { return asset_ != nullptr && !boneNodes_.empty(); }

        void Update(float deltaSeconds);

    private:
        enum class EPlaybackMode
        {
            Loop,
            Manual,
            OneShot,
        };

        struct FBlendState
        {
            std::vector<FRigClipBlendSample> samples;
            std::string syncGroup;
            EPlaybackMode mode = EPlaybackMode::Manual;
            float normalizedTime = 0.0f;
            float playRate = 1.0f;
            float oneShotTime = 0.0f;
            float fadeInSeconds = 0.0f;
            float fadeOutSeconds = 0.0f;
            bool complete = false;
        };

        struct FLayer
        {
            std::string name;
            ERigLayerBlendMode blendMode = ERigLayerBlendMode::Override;
            FRigBoneMask mask;
            FBlendState current;
            FBlendState previous;
            bool hasCurrent = false;
            bool hasPrevious = false;
            float transitionDuration = 0.0f;
            float transitionRemaining = 0.0f;

            float weight = 1.0f;
            float weightStart = 1.0f;
            float weightTarget = 1.0f;
            float weightFadeDuration = 0.0f;
            float weightFadeRemaining = 0.0f;
        };

        struct FAccumBone
        {
            glm::vec3 t{0.0f};
            glm::vec3 s{0.0f};
            glm::vec4 r{0.0f};
            glm::quat referenceR{1.0f, 0.0f, 0.0f, 0.0f};
            float tWeight = 0.0f;
            float rWeight = 0.0f;
            float sWeight = 0.0f;
            bool hasReferenceR = false;
        };

        FLayer* Layer(FRigLayerHandle handle);
        const FLayer* Layer(FRigLayerHandle handle) const;
        static bool SameClipSet(const std::vector<FRigClipBlendSample>& lhs,
                                const std::vector<FRigClipBlendSample>& rhs);
        static float SmoothStep01(float value);
        static float ReferenceDuration(const FBlendState& state);
        static float EnvelopeWeight(const FBlendState& state);

        void BeginState(FLayer& layer, FBlendState state, float fadeSeconds, bool preservePhase);
        void AdvanceState(FBlendState& state, float deltaSeconds);
        void SampleClip(const Assets::FRigClip& clip, float time, FRigPose& outPose);
        void SampleBlend(const FBlendState& state, FRigPose& outPose);
        void BlendTransitions(const FRigPose& previous, const FRigPose& current, float weight, FRigPose& outPose);
        void ApplyLayer(const FLayer& layer, const FRigPose& layerPose, float layerWeight);
        void ApplyPose();

        const Assets::FRigAsset* asset_ = nullptr;
        std::vector<Assets::Node*> boneNodes_;
        Assets::Node* root_ = nullptr;
        std::vector<FLayer> layers_;

        FRigPose workingPose_;
        FRigPose currentPose_;
        FRigPose previousPose_;
        FRigPose transitionPose_;
        FRigPose samplePose_;
        std::vector<FAccumBone> accum_;
    };
} // namespace NextGameplay
