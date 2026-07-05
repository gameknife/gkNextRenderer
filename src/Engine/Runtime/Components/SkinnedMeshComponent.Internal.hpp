#pragma once

// Internal ozz runtime state for SkinnedMeshComponent.cpp.

#include "Engine/Common/CoreMinimal.hpp"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "ozz/animation/runtime/animation.h"
#include "ozz/animation/runtime/blending_job.h"
#include "ozz/animation/runtime/local_to_model_job.h"
#include "ozz/animation/runtime/sampling_job.h"
#include "ozz/animation/runtime/skeleton.h"
#include "ozz/base/maths/simd_quaternion.h"
#include "ozz/base/containers/vector.h"
#include "ozz/base/maths/simd_math.h"
#include "ozz/base/maths/soa_transform.h"
#include "ozz/base/memory/unique_ptr.h"
#include "ozz/base/platform.h"

namespace Runtime
{
    struct SkinnedMeshOzzState
    {
        ozz::unique_ptr<ozz::animation::Skeleton> skeleton;
        std::unordered_map<std::string, ozz::unique_ptr<ozz::animation::Animation>> animations;

        ozz::animation::SamplingJob::Context contextA;
        ozz::animation::SamplingJob::Context contextB;

        ozz::vector<ozz::math::SoaTransform> localsA;
        ozz::vector<ozz::math::SoaTransform> localsB;
        ozz::vector<ozz::math::SoaTransform> blended;
        ozz::vector<ozz::math::Float4x4> models;

        // Mapping from ozz joint index (depth-first) to the matching Assets::Skeleton::Joints[] index.
        // -1 means no matching joint by name (should not happen in practice).
        std::vector<int> ozzToAsset;
    };
}

