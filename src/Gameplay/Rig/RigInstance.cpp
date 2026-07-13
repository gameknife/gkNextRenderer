#include "Gameplay/Rig/RigInstance.h"

#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Runtime/Scene/SceneBuilder.hpp"

#include <algorithm>
#include <cmath>

#include <fmt/format.h>
#include <spdlog/spdlog.h>

namespace NextGameplay
{
    std::shared_ptr<Assets::Node> FRigInstance::Instantiate(
        Assets::Scene& scene,
        const Assets::FRigAsset& asset,
        const FRigInstanceDesc& desc,
        std::vector<Assets::Node*>& outBoneNodes)
    {
        outBoneNodes.assign(asset.bones.size(), nullptr);
        if (asset.bones.empty())
        {
            return nullptr;
        }

        // Bones come parent-first, so parents are always created before children.
        std::vector<std::shared_ptr<Assets::Node>> boneNodes(asset.bones.size());
        for (size_t i = 0; i < asset.bones.size(); ++i)
        {
            const Assets::FRigBone& bone = asset.bones[i];
            auto node = Assets::Node::CreateNode(
                fmt::format("{}/{}", desc.namePrefix, bone.name),
                bone.bindT, bone.bindR, bone.bindS,
                scene.GenerateInstanceId());
            if (bone.parent >= 0)
            {
                node->SetParent(boneNodes[bone.parent]);
            }
            scene.AddNode(node);
            boneNodes[i] = node;
            outBoneNodes[i] = node.get();
        }

        for (size_t partIndex = 0; partIndex < asset.parts.size(); ++partIndex)
        {
            const Assets::FRigPart& part = asset.parts[partIndex];
            if (part.bone < 0 || part.bone >= static_cast<int32_t>(boneNodes.size()) ||
                partIndex >= desc.partModelIds.size())
            {
                SPDLOG_WARN("RIG: instance '{}' part {} skipped (bone {} / {} model ids)",
                            desc.namePrefix, partIndex, part.bone, desc.partModelIds.size());
                continue;
            }
            const std::array<uint32_t, 16> materials = partIndex < desc.partMaterialIds.size()
                                                           ? desc.partMaterialIds[partIndex]
                                                           : std::array<uint32_t, 16>{};
            auto renderNode = Assets::SceneBuilder::CreateRenderNode(
                fmt::format("{}/{}__part{}", desc.namePrefix, asset.bones[part.bone].name, partIndex),
                glm::vec3(0.0f),
                glm::vec3(1.0f),
                scene.GenerateInstanceId(),
                desc.partModelIds[partIndex],
                materials);
            renderNode->SetParent(boneNodes[part.bone]);
            scene.AddNode(renderNode);
        }

        boneNodes[0]->RecalcTransform(true);
        return boneNodes[0];
    }

    void FRigAnimator::Bind(const Assets::FRigAsset* asset, std::vector<Assets::Node*> boneNodes, Assets::Node* root)
    {
        asset_ = asset;
        boneNodes_ = std::move(boneNodes);
        root_ = root;
        current_ = {};
        previous_ = {};
        currentName_.clear();
        fadeDuration_ = 0.0f;
        fadeRemaining_ = 0.0f;
    }

    void FRigAnimator::Play(std::string_view clip, float fadeSeconds)
    {
        if (!asset_ || clip == currentName_)
        {
            return;
        }
        const Assets::FRigClip* next = asset_->FindClip(clip);
        if (!next)
        {
            SPDLOG_WARN("RIG: unknown clip '{}'", std::string(clip));
            return;
        }

        previous_ = current_;
        current_.clip = next;
        current_.time = next->loop && next->duration > 0.0f
                            ? std::fmod(std::abs(phaseOffset_), next->duration)
                            : 0.0f;
        currentName_ = std::string(clip);
        fadeDuration_ = previous_.clip ? std::max(fadeSeconds, 0.0f) : 0.0f;
        fadeRemaining_ = fadeDuration_;
    }

    void FRigAnimator::Advance(FPlayState& state, float deltaSeconds) const
    {
        if (!state.clip)
        {
            return;
        }
        state.time += deltaSeconds * playSpeed_;
        if (state.clip->duration <= 0.0f)
        {
            state.time = 0.0f;
            return;
        }
        if (state.clip->loop)
        {
            state.time = std::fmod(state.time, state.clip->duration);
            if (state.time < 0.0f)
            {
                state.time += state.clip->duration;
            }
        }
        else
        {
            state.time = std::clamp(state.time, 0.0f, state.clip->duration);
        }
    }

    void FRigAnimator::SamplePose(const FPlayState& state, std::vector<FBonePose>& poses) const
    {
        poses.assign(boneNodes_.size(), FBonePose{});
        if (!state.clip)
        {
            return;
        }
        for (const Assets::FRigChannel& channel : state.clip->channels)
        {
            if (channel.bone < 0 || channel.bone >= static_cast<int32_t>(poses.size()))
            {
                continue;
            }
            FBonePose& pose = poses[channel.bone];
            if (!channel.position.Keys.empty())
            {
                pose.t = channel.position.Sample(state.time);
            }
            if (!channel.rotation.Keys.empty())
            {
                pose.r = channel.rotation.Sample(state.time);
            }
            if (!channel.scale.Keys.empty())
            {
                pose.s = channel.scale.Sample(state.time);
            }
        }
    }

    void FRigAnimator::ApplyPose(const std::vector<FBonePose>& poses)
    {
        for (size_t i = 0; i < boneNodes_.size() && i < poses.size(); ++i)
        {
            Assets::Node* node = boneNodes_[i];
            if (!node)
            {
                continue;
            }
            const Assets::FRigBone& bone = asset_->bones[i];
            const FBonePose& pose = poses[i];
            // L_final = L_bind * T(pos) * R(rot) * S(scale); exact for the
            // uniform bind scale the loader enforces.
            node->Translation() = bone.bindT + bone.bindR * (bone.bindS * pose.t);
            node->Rotation() = bone.bindR * pose.r;
            node->Scale() = bone.bindS * pose.s;
        }
        if (root_)
        {
            root_->RecalcTransform(true);
        }
    }

    void FRigAnimator::Update(float deltaSeconds)
    {
        if (!asset_ || boneNodes_.empty() || !current_.clip)
        {
            return;
        }

        Advance(current_, deltaSeconds);
        SamplePose(current_, scratchCurrent_);

        if (fadeRemaining_ > 0.0f && previous_.clip)
        {
            Advance(previous_, deltaSeconds);
            fadeRemaining_ = std::max(0.0f, fadeRemaining_ - deltaSeconds);

            const float x = fadeDuration_ > 0.0f ? 1.0f - fadeRemaining_ / fadeDuration_ : 1.0f;
            const float w = x * x * (3.0f - 2.0f * x); // smoothstep weight of the new clip

            SamplePose(previous_, scratchPrevious_);
            for (size_t i = 0; i < scratchCurrent_.size(); ++i)
            {
                FBonePose& cur = scratchCurrent_[i];
                const FBonePose& prev = scratchPrevious_[i];
                cur.t = glm::mix(prev.t, cur.t, w);
                cur.r = glm::slerp(prev.r, cur.r, w);
                cur.s = glm::mix(prev.s, cur.s, w);
            }
            if (fadeRemaining_ <= 0.0f)
            {
                previous_ = {};
            }
        }

        ApplyPose(scratchCurrent_);
    }
} // namespace NextGameplay
