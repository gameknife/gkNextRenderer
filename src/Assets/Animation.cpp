#include "Animation.hpp"

#include "ozz/animation/runtime/animation.h"
#include "ozz/animation/runtime/local_to_model_job.h"
#include "ozz/animation/runtime/skeleton.h"
#include "ozz/animation/runtime/track.h"
#include "ozz/base/io/archive.h"
#include "ozz/base/io/stream.h"

namespace Assets {

    bool LoadSkeleton(const char* filename, ozz::animation::Skeleton* _skeleton)
    {
        ozz::io::File file(filename, "rb");
        if (!file.opened()) {
            return false;
        }
        ozz::io::IArchive archive(&file);
        if (!archive.TestTag<ozz::animation::Skeleton>()) {
            return false;
        }
        
        archive >> *_skeleton;
        return true;
    }

    bool LoadAnimation(const char* _filename, ozz::animation::Animation* _animation)
    {
        ozz::io::File file(_filename, "rb");
        if (!file.opened()) {
            return false;
        }
        ozz::io::IArchive archive(&file);
        if (!archive.TestTag<ozz::animation::Animation>()) {
            return false;
        }

        // Once the tag is validated, reading cannot fail.
        archive >> *_animation;
        return true;
    }
}
