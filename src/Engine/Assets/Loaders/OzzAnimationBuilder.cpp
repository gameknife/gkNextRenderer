#include "Engine/Assets/Loaders/OzzAnimationBuilder.h"

#if WITH_OZZ
#include "Engine/Assets/Data/Skeleton.hpp"
#include "Engine/Assets/Core/Model.hpp"

#include "ozz/animation/offline/animation_builder.h"
#include "ozz/animation/offline/raw_animation.h"
#include "ozz/animation/offline/raw_skeleton.h"
#include "ozz/animation/offline/skeleton_builder.h"
#include "ozz/animation/runtime/animation.h"
#include "ozz/animation/runtime/skeleton.h"
#include "ozz/base/maths/soa_transform.h"
#include "ozz/base/maths/simd_math.h"
#include "ozz/base/platform.h"

#include <spdlog/spdlog.h>
#include <algorithm>
#include <functional>
#include <unordered_map>

namespace Assets
{
    namespace
    {
        ozz::math::Float3 ToOzz(const glm::vec3& v)
        {
            return ozz::math::Float3(v.x, v.y, v.z);
        }

        ozz::math::Quaternion ToOzz(const glm::quat& q)
        {
            return ozz::math::Quaternion(q.x, q.y, q.z, q.w);
        }

        void BuildRawJoint(const Skeleton& src, int jointIndex, ozz::animation::offline::RawSkeleton::Joint& dst)
        {
            const Joint& srcJoint = src.Joints[jointIndex];
            dst.name = srcJoint.Name.c_str();
            dst.transform.translation = ToOzz(srcJoint.Translation);
            dst.transform.rotation = ToOzz(srcJoint.Rotation);
            dst.transform.scale = ToOzz(srcJoint.Scale);

            dst.children.resize(srcJoint.Children.size());
            for (size_t i = 0; i < srcJoint.Children.size(); ++i)
            {
                BuildRawJoint(src, srcJoint.Children[i], dst.children[i]);
            }
        }
    }

    ozz::unique_ptr<ozz::animation::Skeleton> BuildOzzSkeleton(const Skeleton& src)
    {
        ozz::animation::offline::RawSkeleton raw;

        // Collect roots (joints whose ParentIndex == -1).
        std::vector<int> rootIndices;
        for (size_t i = 0; i < src.Joints.size(); ++i)
        {
            if (src.Joints[i].ParentIndex == -1)
            {
                rootIndices.push_back(static_cast<int>(i));
            }
        }
        raw.roots.resize(rootIndices.size());
        for (size_t i = 0; i < rootIndices.size(); ++i)
        {
            BuildRawJoint(src, rootIndices[i], raw.roots[i]);
        }

        if (!raw.Validate())
        {
            SPDLOG_ERROR("BuildOzzSkeleton: RawSkeleton failed validation (skeleton='{}', joints={})",
                         src.Name, src.Joints.size());
            return {};
        }

        ozz::animation::offline::SkeletonBuilder builder;
        return builder(raw);
    }

    ozz::unique_ptr<ozz::animation::Animation> BuildOzzAnimation(
        const std::string& animName,
        const ozz::animation::Skeleton& ozzSkeleton,
        const std::vector<AnimationTrack>& allTracks)
    {
        // Index project tracks by joint name for the requested animation.
        std::unordered_map<std::string, const AnimationTrack*> byName;
        float duration = 0.0f;
        for (const auto& track : allTracks)
        {
            if (track.AnimationName != animName)
            {
                continue;
            }
            byName.emplace(track.NodeName_, &track);
            duration = std::max(duration, track.Duration_);
        }

        if (byName.empty() || duration <= 0.0f)
        {
            return {};
        }

        ozz::animation::offline::RawAnimation raw;
        raw.name = animName.c_str();
        raw.duration = duration;
        raw.tracks.resize(ozzSkeleton.num_joints());

        const auto names = ozzSkeleton.joint_names();
        const auto restPoses = ozzSkeleton.joint_rest_poses();

        for (int i = 0; i < ozzSkeleton.num_joints(); ++i)
        {
            auto& outTrack = raw.tracks[i];
            const auto it = byName.find(names[i]);

            if (it == byName.end())
            {
                // No animation track: emit a single key at t=0 from rest pose so the
                // joint is well-defined for sampling.
                const ozz::math::SoaTransform& soa = restPoses[i / 4];
                const int lane = i % 4;

                auto extractLane = [lane](const ozz::math::SimdFloat4& v, float* out)
                {
                    alignas(16) float tmp[4];
                    ozz::math::StorePtr(v, tmp);
                    *out = tmp[lane];
                };

                ozz::math::Float3 t;
                extractLane(soa.translation.x, &t.x);
                extractLane(soa.translation.y, &t.y);
                extractLane(soa.translation.z, &t.z);
                ozz::math::Quaternion r;
                extractLane(soa.rotation.x, &r.x);
                extractLane(soa.rotation.y, &r.y);
                extractLane(soa.rotation.z, &r.z);
                extractLane(soa.rotation.w, &r.w);
                ozz::math::Float3 s;
                extractLane(soa.scale.x, &s.x);
                extractLane(soa.scale.y, &s.y);
                extractLane(soa.scale.z, &s.z);

                outTrack.translations.push_back({0.0f, t});
                outTrack.rotations.push_back({0.0f, r});
                outTrack.scales.push_back({0.0f, s});
                continue;
            }

            const AnimationTrack& src = *it->second;

            auto clampTime = [duration](float t) -> float
            {
                return std::clamp(t, 0.0f, duration);
            };

            if (src.TranslationChannel.Keys.empty())
            {
                outTrack.translations.push_back({0.0f, ozz::math::Float3::zero()});
            }
            else
            {
                outTrack.translations.reserve(src.TranslationChannel.Keys.size());
                for (const auto& k : src.TranslationChannel.Keys)
                {
                    outTrack.translations.push_back({clampTime(k.Time), ToOzz(k.Value)});
                }
            }

            if (src.RotationChannel.Keys.empty())
            {
                outTrack.rotations.push_back({0.0f, ozz::math::Quaternion::identity()});
            }
            else
            {
                outTrack.rotations.reserve(src.RotationChannel.Keys.size());
                for (const auto& k : src.RotationChannel.Keys)
                {
                    outTrack.rotations.push_back({clampTime(k.Time), ToOzz(k.Value)});
                }
            }

            if (src.ScaleChannel.Keys.empty())
            {
                outTrack.scales.push_back({0.0f, ozz::math::Float3::one()});
            }
            else
            {
                outTrack.scales.reserve(src.ScaleChannel.Keys.size());
                for (const auto& k : src.ScaleChannel.Keys)
                {
                    outTrack.scales.push_back({clampTime(k.Time), ToOzz(k.Value)});
                }
            }
        }

        if (!raw.Validate())
        {
            SPDLOG_ERROR("BuildOzzAnimation: RawAnimation '{}' failed validation", animName);
            return {};
        }

        ozz::animation::offline::AnimationBuilder builder;
        return builder(raw);
    }
}
#endif
