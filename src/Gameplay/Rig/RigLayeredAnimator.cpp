#include "Gameplay/Rig/RigLayeredAnimator.h"

#include "Engine/Assets/Core/Node.hpp"

#include <algorithm>
#include <cmath>

#include <spdlog/spdlog.h>

namespace NextGameplay
{
    namespace
    {
        constexpr float kWeightEpsilon = 1.0e-6f;
        const glm::quat kIdentityRotation(1.0f, 0.0f, 0.0f, 0.0f);
        const glm::vec3 kIdentityScale(1.0f);
    }

    void FRigPose::Reset(size_t boneCount)
    {
        bones.resize(boneCount);
        std::fill(bones.begin(), bones.end(), FRigBonePose{});
    }

    FRigBoneMask FRigBoneMask::FullBody(const Assets::FRigAsset& asset)
    {
        FRigBoneMask mask;
        mask.weights.assign(asset.bones.size(), 1.0f);
        return mask;
    }

    FRigBoneMask FRigBoneMask::FromSubtree(const Assets::FRigAsset& asset, std::string_view rootBone, float weight)
    {
        FRigBoneMask mask;
        mask.weights.assign(asset.bones.size(), 0.0f);
        const int32_t rootIndex = asset.FindBone(rootBone);
        if (rootIndex < 0)
        {
            SPDLOG_WARN("RIG: bone mask root '{}' does not exist", std::string(rootBone));
            return mask;
        }

        std::vector<int32_t> pending{rootIndex};
        while (!pending.empty())
        {
            const int32_t boneIndex = pending.back();
            pending.pop_back();
            if (boneIndex < 0 || boneIndex >= static_cast<int32_t>(asset.bones.size()))
            {
                continue;
            }
            mask.weights[boneIndex] = std::clamp(weight, 0.0f, 1.0f);
            for (int32_t child : asset.bones[boneIndex].children)
            {
                pending.push_back(child);
            }
        }
        return mask;
    }

    bool FRigBoneMask::SetBoneWeight(const Assets::FRigAsset& asset, std::string_view boneName, float weight)
    {
        const int32_t boneIndex = asset.FindBone(boneName);
        if (boneIndex < 0 || boneIndex >= static_cast<int32_t>(weights.size()))
        {
            return false;
        }
        weights[boneIndex] = std::clamp(weight, 0.0f, 1.0f);
        return true;
    }

    float FRigBoneMask::Weight(size_t boneIndex) const
    {
        return boneIndex < weights.size() ? std::clamp(weights[boneIndex], 0.0f, 1.0f) : 0.0f;
    }

    void FRigLayeredAnimator::Bind(const Assets::FRigAsset* asset, std::vector<Assets::Node*> boneNodes,
                                   Assets::Node* root)
    {
        asset_ = asset;
        boneNodes_ = std::move(boneNodes);
        root_ = root;
        layers_.clear();

        const size_t boneCount = asset_ ? std::min(asset_->bones.size(), boneNodes_.size()) : 0;
        workingPose_.Reset(boneCount);
        currentPose_.Reset(boneCount);
        previousPose_.Reset(boneCount);
        transitionPose_.Reset(boneCount);
        samplePose_.Reset(boneCount);
        accum_.resize(boneCount);
    }

    FRigLayerHandle FRigLayeredAnimator::CreateLayer(std::string name, ERigLayerBlendMode blendMode,
                                                     FRigBoneMask mask)
    {
        if (!IsBound() || mask.weights.size() != boneNodes_.size())
        {
            SPDLOG_WARN("RIG: cannot create layer '{}' with {} mask weights for {} bones", name,
                        mask.weights.size(), boneNodes_.size());
            return invalidRigLayerHandle;
        }
        FLayer layer;
        layer.name = std::move(name);
        layer.blendMode = blendMode;
        layer.mask = std::move(mask);
        layers_.push_back(std::move(layer));
        return static_cast<FRigLayerHandle>(layers_.size() - 1);
    }

    FRigLayeredAnimator::FLayer* FRigLayeredAnimator::Layer(FRigLayerHandle handle)
    {
        return handle < layers_.size() ? &layers_[handle] : nullptr;
    }

    const FRigLayeredAnimator::FLayer* FRigLayeredAnimator::Layer(FRigLayerHandle handle) const
    {
        return handle < layers_.size() ? &layers_[handle] : nullptr;
    }

    bool FRigLayeredAnimator::SetLayerWeight(FRigLayerHandle handle, float weight, float fadeSeconds)
    {
        FLayer* layer = Layer(handle);
        if (!layer)
        {
            return false;
        }

        layer->weightStart = layer->weight;
        layer->weightTarget = std::clamp(weight, 0.0f, 1.0f);
        layer->weightFadeDuration = std::max(fadeSeconds, 0.0f);
        layer->weightFadeRemaining = layer->weightFadeDuration;
        if (layer->weightFadeDuration <= 0.0f)
        {
            layer->weight = layer->weightTarget;
        }
        return true;
    }

    bool FRigLayeredAnimator::SameClipSet(const std::vector<FRigClipBlendSample>& lhs,
                                          const std::vector<FRigClipBlendSample>& rhs)
    {
        if (lhs.size() != rhs.size())
        {
            return false;
        }
        for (size_t i = 0; i < lhs.size(); ++i)
        {
            if (lhs[i].clip != rhs[i].clip)
            {
                return false;
            }
        }
        return true;
    }

    void FRigLayeredAnimator::BeginState(FLayer& layer, FBlendState state, float fadeSeconds, bool preservePhase)
    {
        if (preservePhase && layer.hasCurrent)
        {
            state.normalizedTime = layer.current.normalizedTime;
        }

        if (layer.hasCurrent && fadeSeconds > 0.0f)
        {
            layer.previous = layer.current;
            layer.hasPrevious = true;
            layer.transitionDuration = fadeSeconds;
            layer.transitionRemaining = fadeSeconds;
        }
        else
        {
            layer.previous = {};
            layer.hasPrevious = false;
            layer.transitionDuration = 0.0f;
            layer.transitionRemaining = 0.0f;
        }
        layer.current = std::move(state);
        layer.hasCurrent = true;
    }

    bool FRigLayeredAnimator::SetLoopBlend(FRigLayerHandle handle, std::vector<FRigClipBlendSample> samples,
                                           std::string syncGroup, float playRate, float fadeSeconds)
    {
        FLayer* layer = Layer(handle);
        if (!layer || samples.empty())
        {
            return false;
        }
        samples.erase(std::remove_if(samples.begin(), samples.end(),
                                     [](const FRigClipBlendSample& sample) { return sample.clip == nullptr; }),
                      samples.end());
        if (samples.empty())
        {
            return false;
        }

        if (layer->hasCurrent && layer->current.mode == EPlaybackMode::Loop &&
            layer->current.syncGroup == syncGroup && SameClipSet(layer->current.samples, samples))
        {
            layer->current.samples = std::move(samples);
            layer->current.playRate = playRate;
            return true;
        }

        FBlendState state;
        state.samples = std::move(samples);
        state.syncGroup = std::move(syncGroup);
        state.mode = EPlaybackMode::Loop;
        state.playRate = playRate;
        const bool preservePhase =
            layer->hasCurrent && layer->current.mode == EPlaybackMode::Loop &&
            layer->current.syncGroup == state.syncGroup;
        BeginState(*layer, std::move(state), std::max(fadeSeconds, 0.0f), preservePhase);
        return true;
    }

    bool FRigLayeredAnimator::SetStaticBlend(FRigLayerHandle handle, std::vector<FRigClipBlendSample> samples,
                                             float fadeSeconds)
    {
        return SetManualBlend(handle, std::move(samples), 0.0f, fadeSeconds);
    }

    bool FRigLayeredAnimator::SetManualBlend(FRigLayerHandle handle, std::vector<FRigClipBlendSample> samples,
                                             float normalizedTime, float fadeSeconds)
    {
        FLayer* layer = Layer(handle);
        if (!layer || samples.empty())
        {
            return false;
        }
        samples.erase(std::remove_if(samples.begin(), samples.end(),
                                     [](const FRigClipBlendSample& sample) { return sample.clip == nullptr; }),
                      samples.end());
        if (samples.empty())
        {
            return false;
        }

        const float clampedTime = std::clamp(normalizedTime, 0.0f, 1.0f);
        if (layer->hasCurrent && layer->current.mode == EPlaybackMode::Manual &&
            SameClipSet(layer->current.samples, samples))
        {
            layer->current.samples = std::move(samples);
            layer->current.normalizedTime = clampedTime;
            return true;
        }

        FBlendState state;
        state.samples = std::move(samples);
        state.mode = EPlaybackMode::Manual;
        state.normalizedTime = clampedTime;
        BeginState(*layer, std::move(state), std::max(fadeSeconds, 0.0f), false);
        return true;
    }

    bool FRigLayeredAnimator::PlayOneShot(FRigLayerHandle handle, const Assets::FRigClip* clip, float playRate,
                                          float fadeInSeconds, float fadeOutSeconds, bool restartIfSame)
    {
        FLayer* layer = Layer(handle);
        if (!layer || !clip)
        {
            return false;
        }
        if (!restartIfSame && layer->hasCurrent && layer->current.mode == EPlaybackMode::OneShot &&
            layer->current.samples.size() == 1 && layer->current.samples.front().clip == clip &&
            !layer->current.complete)
        {
            return true;
        }

        FBlendState state;
        state.samples = {{clip, 1.0f}};
        state.mode = EPlaybackMode::OneShot;
        state.playRate = std::max(playRate, 0.0f);
        state.fadeInSeconds = std::max(fadeInSeconds, 0.0f);
        state.fadeOutSeconds = std::max(fadeOutSeconds, 0.0f);
        BeginState(*layer, std::move(state), 0.0f, false);
        return true;
    }

    bool FRigLayeredAnimator::IsOneShotComplete(FRigLayerHandle handle) const
    {
        const FLayer* layer = Layer(handle);
        return layer && layer->hasCurrent && layer->current.mode == EPlaybackMode::OneShot &&
               layer->current.complete;
    }

    const Assets::FRigClip* FRigLayeredAnimator::DominantClip(FRigLayerHandle handle) const
    {
        const FLayer* layer = Layer(handle);
        if (!layer || !layer->hasCurrent)
        {
            return nullptr;
        }
        const FRigClipBlendSample* best = nullptr;
        for (const FRigClipBlendSample& sample : layer->current.samples)
        {
            if (sample.clip && (!best || sample.weight > best->weight))
            {
                best = &sample;
            }
        }
        return best ? best->clip : nullptr;
    }

    float FRigLayeredAnimator::SmoothStep01(float value)
    {
        const float x = std::clamp(value, 0.0f, 1.0f);
        return x * x * (3.0f - 2.0f * x);
    }

    float FRigLayeredAnimator::ReferenceDuration(const FBlendState& state)
    {
        const FRigClipBlendSample* best = nullptr;
        for (const FRigClipBlendSample& sample : state.samples)
        {
            if (sample.clip && sample.clip->duration > 0.0f &&
                (!best || sample.weight > best->weight))
            {
                best = &sample;
            }
        }
        return best ? best->clip->duration : 0.0f;
    }

    float FRigLayeredAnimator::EnvelopeWeight(const FBlendState& state)
    {
        if (state.mode != EPlaybackMode::OneShot)
        {
            return 1.0f;
        }
        const float duration = ReferenceDuration(state);
        if (duration <= 0.0f || state.complete)
        {
            return 0.0f;
        }

        float weight = 1.0f;
        if (state.fadeInSeconds > 0.0f)
        {
            weight = std::min(weight, state.oneShotTime / state.fadeInSeconds);
        }
        if (state.fadeOutSeconds > 0.0f)
        {
            weight = std::min(weight, (duration - state.oneShotTime) / state.fadeOutSeconds);
        }
        return std::clamp(weight, 0.0f, 1.0f);
    }

    void FRigLayeredAnimator::AdvanceState(FBlendState& state, float deltaSeconds)
    {
        const float dt = std::max(deltaSeconds, 0.0f);
        if (state.mode == EPlaybackMode::Loop)
        {
            const float duration = ReferenceDuration(state);
            if (duration > 0.0f)
            {
                state.normalizedTime =
                    std::fmod(state.normalizedTime + dt * state.playRate / duration, 1.0f);
                if (state.normalizedTime < 0.0f)
                {
                    state.normalizedTime += 1.0f;
                }
            }
        }
        else if (state.mode == EPlaybackMode::OneShot && !state.complete)
        {
            const float duration = ReferenceDuration(state);
            state.oneShotTime += dt * state.playRate;
            if (duration <= 0.0f || state.oneShotTime >= duration)
            {
                state.oneShotTime = std::max(duration, 0.0f);
                state.normalizedTime = 1.0f;
                state.complete = true;
            }
            else
            {
                state.normalizedTime = state.oneShotTime / duration;
            }
        }
    }

    void FRigLayeredAnimator::SampleClip(const Assets::FRigClip& clip, float time, FRigPose& outPose)
    {
        outPose.Reset(workingPose_.bones.size());
        for (const Assets::FRigChannel& channel : clip.channels)
        {
            if (channel.bone < 0 || channel.bone >= static_cast<int32_t>(outPose.bones.size()))
            {
                continue;
            }
            FRigBonePose& pose = outPose.bones[channel.bone];
            if (!channel.position.Keys.empty())
            {
                pose.t = channel.position.Sample(time);
                pose.Mark(ERigPoseComponent::Position);
            }
            if (!channel.rotation.Keys.empty())
            {
                pose.r = glm::normalize(channel.rotation.Sample(time));
                pose.Mark(ERigPoseComponent::Rotation);
            }
            if (!channel.scale.Keys.empty())
            {
                pose.s = channel.scale.Sample(time);
                pose.Mark(ERigPoseComponent::Scale);
            }
        }
    }

    void FRigLayeredAnimator::SampleBlend(const FBlendState& state, FRigPose& outPose)
    {
        outPose.Reset(workingPose_.bones.size());
        accum_.resize(outPose.bones.size());
        std::fill(accum_.begin(), accum_.end(), FAccumBone{});

        for (const FRigClipBlendSample& blendSample : state.samples)
        {
            if (!blendSample.clip || blendSample.weight <= kWeightEpsilon)
            {
                continue;
            }
            const Assets::FRigClip& clip = *blendSample.clip;
            float time = 0.0f;
            if (clip.duration > 0.0f)
            {
                time = std::clamp(state.normalizedTime, 0.0f, 1.0f) * clip.duration;
                if (state.mode == EPlaybackMode::Loop && state.normalizedTime >= 1.0f)
                {
                    time = 0.0f;
                }
            }
            SampleClip(clip, time, samplePose_);

            for (size_t boneIndex = 0; boneIndex < samplePose_.bones.size(); ++boneIndex)
            {
                const FRigBonePose& sample = samplePose_.bones[boneIndex];
                FAccumBone& accum = accum_[boneIndex];
                const float weight = blendSample.weight;
                if (sample.Has(ERigPoseComponent::Position))
                {
                    accum.t += sample.t * weight;
                    accum.tWeight += weight;
                }
                if (sample.Has(ERigPoseComponent::Rotation))
                {
                    glm::quat rotation = sample.r;
                    if (!accum.hasReferenceR)
                    {
                        accum.referenceR = rotation;
                        accum.hasReferenceR = true;
                    }
                    else if (glm::dot(accum.referenceR, rotation) < 0.0f)
                    {
                        rotation = -rotation;
                    }
                    accum.r += glm::vec4(rotation.w, rotation.x, rotation.y, rotation.z) * weight;
                    accum.rWeight += weight;
                }
                if (sample.Has(ERigPoseComponent::Scale))
                {
                    accum.s += sample.s * weight;
                    accum.sWeight += weight;
                }
            }
        }

        for (size_t boneIndex = 0; boneIndex < outPose.bones.size(); ++boneIndex)
        {
            FRigBonePose& pose = outPose.bones[boneIndex];
            const FAccumBone& accum = accum_[boneIndex];
            if (accum.tWeight > kWeightEpsilon)
            {
                pose.t = accum.t / accum.tWeight;
                pose.Mark(ERigPoseComponent::Position);
            }
            if (accum.rWeight > kWeightEpsilon)
            {
                const glm::vec4 q = accum.r / accum.rWeight;
                const glm::quat rotation(q.x, q.y, q.z, q.w);
                pose.r = glm::length(q) > kWeightEpsilon ? glm::normalize(rotation) : kIdentityRotation;
                pose.Mark(ERigPoseComponent::Rotation);
            }
            if (accum.sWeight > kWeightEpsilon)
            {
                pose.s = accum.s / accum.sWeight;
                pose.Mark(ERigPoseComponent::Scale);
            }
        }
    }

    void FRigLayeredAnimator::BlendTransitions(const FRigPose& previous, const FRigPose& current, float weight,
                                               FRigPose& outPose)
    {
        outPose.Reset(workingPose_.bones.size());
        const float w = SmoothStep01(weight);
        for (size_t i = 0; i < outPose.bones.size(); ++i)
        {
            const FRigBonePose& prev = previous.bones[i];
            const FRigBonePose& cur = current.bones[i];
            FRigBonePose& out = outPose.bones[i];

            if (prev.Has(ERigPoseComponent::Position) || cur.Has(ERigPoseComponent::Position))
            {
                out.t = glm::mix(prev.Has(ERigPoseComponent::Position) ? prev.t : glm::vec3(0.0f),
                                 cur.Has(ERigPoseComponent::Position) ? cur.t : glm::vec3(0.0f), w);
                out.Mark(ERigPoseComponent::Position);
            }
            if (prev.Has(ERigPoseComponent::Rotation) || cur.Has(ERigPoseComponent::Rotation))
            {
                glm::quat prevR = prev.Has(ERigPoseComponent::Rotation) ? prev.r : kIdentityRotation;
                glm::quat curR = cur.Has(ERigPoseComponent::Rotation) ? cur.r : kIdentityRotation;
                if (glm::dot(prevR, curR) < 0.0f)
                {
                    curR = -curR;
                }
                out.r = glm::normalize(glm::slerp(prevR, curR, w));
                out.Mark(ERigPoseComponent::Rotation);
            }
            if (prev.Has(ERigPoseComponent::Scale) || cur.Has(ERigPoseComponent::Scale))
            {
                out.s = glm::mix(prev.Has(ERigPoseComponent::Scale) ? prev.s : kIdentityScale,
                                 cur.Has(ERigPoseComponent::Scale) ? cur.s : kIdentityScale, w);
                out.Mark(ERigPoseComponent::Scale);
            }
        }
    }

    void FRigLayeredAnimator::ApplyLayer(const FLayer& layer, const FRigPose& layerPose, float layerWeight)
    {
        for (size_t i = 0; i < workingPose_.bones.size(); ++i)
        {
            const float weight = std::clamp(layerWeight * layer.mask.Weight(i), 0.0f, 1.0f);
            if (weight <= kWeightEpsilon)
            {
                continue;
            }
            FRigBonePose& out = workingPose_.bones[i];
            const FRigBonePose& value = layerPose.bones[i];

            if (value.Has(ERigPoseComponent::Position))
            {
                if (layer.blendMode == ERigLayerBlendMode::Additive)
                {
                    out.t += value.t * weight;
                }
                else
                {
                    out.t = glm::mix(out.t, value.t, weight);
                }
                out.Mark(ERigPoseComponent::Position);
            }
            if (value.Has(ERigPoseComponent::Rotation))
            {
                const glm::quat weighted = glm::normalize(glm::slerp(kIdentityRotation, value.r, weight));
                if (layer.blendMode == ERigLayerBlendMode::Additive)
                {
                    out.r = glm::normalize(out.r * weighted);
                }
                else
                {
                    glm::quat target = value.r;
                    if (glm::dot(out.r, target) < 0.0f)
                    {
                        target = -target;
                    }
                    out.r = glm::normalize(glm::slerp(out.r, target, weight));
                }
                out.Mark(ERigPoseComponent::Rotation);
            }
            if (value.Has(ERigPoseComponent::Scale))
            {
                if (layer.blendMode == ERigLayerBlendMode::Additive)
                {
                    out.s *= glm::mix(kIdentityScale, value.s, weight);
                }
                else
                {
                    out.s = glm::mix(out.s, value.s, weight);
                }
                out.Mark(ERigPoseComponent::Scale);
            }
        }
    }

    void FRigLayeredAnimator::ApplyPose()
    {
        if (!asset_)
        {
            return;
        }
        for (size_t i = 0; i < workingPose_.bones.size(); ++i)
        {
            Assets::Node* node = boneNodes_[i];
            if (!node)
            {
                continue;
            }
            const Assets::FRigBone& bone = asset_->bones[i];
            const FRigBonePose& pose = workingPose_.bones[i];
            node->Translation() = bone.bindT + bone.bindR * (bone.bindS * pose.t);
            node->Rotation() = glm::normalize(bone.bindR * pose.r);
            node->Scale() = bone.bindS * pose.s;
        }
        if (root_)
        {
            root_->RecalcTransform(true);
        }
    }

    void FRigLayeredAnimator::Update(float deltaSeconds)
    {
        if (!IsBound())
        {
            return;
        }

        workingPose_.Reset(std::min(asset_->bones.size(), boneNodes_.size()));
        for (FLayer& layer : layers_)
        {
            if (layer.weightFadeRemaining > 0.0f)
            {
                layer.weightFadeRemaining = std::max(0.0f, layer.weightFadeRemaining - deltaSeconds);
                const float progress = layer.weightFadeDuration > 0.0f
                                           ? 1.0f - layer.weightFadeRemaining / layer.weightFadeDuration
                                           : 1.0f;
                layer.weight = glm::mix(layer.weightStart, layer.weightTarget, SmoothStep01(progress));
            }

            if (!layer.hasCurrent)
            {
                continue;
            }
            AdvanceState(layer.current, deltaSeconds);
            SampleBlend(layer.current, currentPose_);

            const FRigPose* poseToApply = &currentPose_;
            if (layer.hasPrevious && layer.transitionRemaining > 0.0f)
            {
                AdvanceState(layer.previous, deltaSeconds);
                SampleBlend(layer.previous, previousPose_);
                layer.transitionRemaining = std::max(0.0f, layer.transitionRemaining - deltaSeconds);
                const float progress = layer.transitionDuration > 0.0f
                                           ? 1.0f - layer.transitionRemaining / layer.transitionDuration
                                           : 1.0f;
                BlendTransitions(previousPose_, currentPose_, progress, transitionPose_);
                poseToApply = &transitionPose_;
                if (layer.transitionRemaining <= 0.0f)
                {
                    layer.previous = {};
                    layer.hasPrevious = false;
                }
            }

            const float effectiveWeight = layer.weight * EnvelopeWeight(layer.current);
            ApplyLayer(layer, *poseToApply, effectiveWeight);
        }
        ApplyPose();
    }
} // namespace NextGameplay
