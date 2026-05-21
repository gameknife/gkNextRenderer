#pragma once
#include "Engine/Common/CoreMinimal.hpp"

#if WITH_OZZ
#include "ozz/base/memory/unique_ptr.h"

namespace ozz
{
    namespace animation
    {
        class Skeleton;
        class Animation;
    }
}

namespace Assets
{
    struct Skeleton;
    struct AnimationTrack;
}

namespace Assets
{
    // Builds a runtime ozz::animation::Skeleton from the project's glm-based Skeleton.
    // Joint hierarchy is recreated; runtime ozz joints are stored in depth-first order
    // (determined by SkeletonBuilder).
    ozz::unique_ptr<ozz::animation::Skeleton> BuildOzzSkeleton(const Skeleton& src);

    // Builds a runtime ozz::animation::Animation for one named animation.
    // Only tracks whose AnimationName matches `animName` and whose NodeName matches a joint
    // in `ozzSkeleton` will be folded in. Joints without a track sample their rest pose.
    ozz::unique_ptr<ozz::animation::Animation> BuildOzzAnimation(
        const std::string& animName,
        const ozz::animation::Skeleton& ozzSkeleton,
        const std::vector<AnimationTrack>& allTracks);
}
#endif
