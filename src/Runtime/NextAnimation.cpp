#include "NextAnimation.h"

#include "Engine.hpp"
#include "animation/runtime/sampling_job.h"
#include "animation/runtime/animation.h"
#include "animation/runtime/local_to_model_job.h"
#include "animation/runtime/skeleton.h"
#include "animation/runtime/sampling_job.h"
#include "base/maths/soa_transform.h"
#include "base/maths/simd_math.h"
#include "base/containers/vector.h"
#include "Utilities/FileHelper.hpp"

std::unique_ptr<ozz::animation::SamplingJob::Context> s_context_;

NextAnimation::NextAnimation()
{
}

NextAnimation::~NextAnimation()
{
}

void NextAnimation::Start()
{
    // test ozz animation
    skeleton_.reset(new ozz::animation::Skeleton());
    animation_.reset(new ozz::animation::Animation());
    s_context_.reset(new ozz::animation::SamplingJob::Context());
    
    Assets::LoadSkeleton(Utilities::FileHelper::GetPlatformFilePath("assets/anims/skeleton.ozz").c_str(), skeleton_.get() );
    Assets::LoadAnimation(Utilities::FileHelper::GetPlatformFilePath("assets/anims/animation.ozz").c_str(), animation_.get() );

    // Allocates runtime buffers.
    const int num_joints = skeleton_->num_joints();
    s_context_->Resize(num_joints);
}

float absoluteTimer = 0;
void NextAnimation::Tick(double DeltaSeconds)
{
    absoluteTimer += float(DeltaSeconds);
    float timeRatio = glm::fract(absoluteTimer * 0.1f);
    
    // Buffer of local transforms as sampled from animation_.
    ozz::vector<ozz::math::SoaTransform> locals_;
    // Buffer of model space matrices.
    ozz::vector<ozz::math::Float4x4> models_;

    const int num_soa_joints = skeleton_->num_soa_joints();
    locals_.resize(num_soa_joints);
    const int num_joints = skeleton_->num_joints();
    models_.resize(num_joints);
    
    // Samples optimized animation at t = animation_time_.
    ozz::animation::SamplingJob sampling_job;
    sampling_job.animation = animation_.get();
    sampling_job.context = s_context_.get();
    sampling_job.ratio = timeRatio;
    sampling_job.output = make_span(locals_);
    if (!sampling_job.Run()) {

    }

    // Converts from local space to model space matrices.
    ozz::animation::LocalToModelJob ltm_job;
    ltm_job.skeleton = skeleton_.get();
    ltm_job.input = make_span(locals_);
    ltm_job.output = make_span(models_);
    if (!ltm_job.Run()) {

    }
    
    const ozz::span<const int16_t>& parents = skeleton_->joint_parents();

    int instances = 0;
    for (int i = 0; i < num_joints; ++i)
    {
        // Root isn't rendered.
        const int16_t parent_id = parents[i];
        if (parent_id == ozz::animation::Skeleton::kNoParent) {
            continue;
        }

        // Selects joint matrices.
        const ozz::math::Float4x4& parent = models_[parent_id];
        const ozz::math::Float4x4& current = models_[i];

        ozz::math::SimdFloat4 p0 = ozz::math::TransformPoint(parent, {0,0,0,1});
        ozz::math::SimdFloat4 p1 = ozz::math::TransformPoint(current, {0,0,0,1});
#if __APPLE__
        glm::vec3 from = {p0.x, p0.y, p0.z};
        glm::vec3 to = {p1.x, p1.y, p1.z};
#else
        glm::vec3 from = {p0.m128_f32[0], p0.m128_f32[1], p0.m128_f32[2]};
        glm::vec3 to = {p1.m128_f32[0], p1.m128_f32[1], p1.m128_f32[2]};
#endif
        NextEngine::GetInstance()->DrawAuxLine(from, to, glm::vec4(1.0f, 1.0f, 1.0f, 1.0f), 1.0f);
        NextEngine::GetInstance()->DrawAuxPoint(to, glm::vec4(1.0f, 0.0f, 0.0f, 1.0f), 2.0f);
    }
}

void NextAnimation::Stop()
{
    skeleton_.reset();
    animation_.reset();
    s_context_.reset();
}
