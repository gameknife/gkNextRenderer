#include "Engine/Assets/Data/Animation.hpp"

#include "ozz/animation/runtime/animation.h"
#include "ozz/animation/runtime/local_to_model_job.h"
#include "ozz/animation/runtime/skeleton.h"
#include "ozz/animation/runtime/track.h"
#include "ozz/base/io/archive.h"
#include "ozz/base/io/stream.h"

namespace Assets {

    bool LoadSkeleton(const char* filename, ozz::animation::Skeleton* skeleton)
    {
        ozz::io::File file(filename, "rb");
        if (!file.opened()) {
            return false;
        }
        ozz::io::IArchive archive(&file);
        if (!archive.TestTag<ozz::animation::Skeleton>()) {
            return false;
        }
        
        archive >> *skeleton;
        return true;
    }

    bool LoadAnimation(const char* filename, ozz::animation::Animation* animation)
    {
        ozz::io::File file(filename, "rb");
        if (!file.opened()) {
            return false;
        }
        ozz::io::IArchive archive(&file);
        if (!archive.TestTag<ozz::animation::Animation>()) {
            return false;
        }

        // Once the tag is validated, reading cannot fail.
        archive >> *animation;
        return true;
    }
}
