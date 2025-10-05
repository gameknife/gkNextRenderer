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

std::unique_ptr<ozz::animation::SamplingJob::Context> SContext;

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
    SContext.reset(new ozz::animation::SamplingJob::Context());
    
    Assets::LoadSkeleton(Utilities::FileHelper::GetPlatformFilePath("assets/anims/skeleton.ozz").c_str(), skeleton_.get() );
    Assets::LoadAnimation(Utilities::FileHelper::GetPlatformFilePath("assets/anims/animation.ozz").c_str(), animation_.get() );

    // Allocates runtime buffers.
    const int numJoints = skeleton_->num_joints();
    SContext->Resize(numJoints);
}

float AbsoluteTimer = 0;
void NextAnimation::Tick(double deltaSeconds)
{
    AbsoluteTimer += float(deltaSeconds);
    float timeRatio = glm::fract(AbsoluteTimer * 0.1f);
    
    // Buffer of local transforms as sampled from animation_.
    ozz::vector<ozz::math::SoaTransform> locals;
    // Buffer of model space matrices.
    ozz::vector<ozz::math::Float4x4> models;

    const int numSoaJoints = skeleton_->num_soa_joints();
    locals.resize(numSoaJoints);
    const int numJoints = skeleton_->num_joints();
    models.resize(numJoints);
    
    // Samples optimized animation at t = animation_time_.
    ozz::animation::SamplingJob samplingJob;
    samplingJob.animation = animation_.get();
    samplingJob.context = SContext.get();
    samplingJob.ratio = timeRatio;
    samplingJob.output = make_span(locals);
    if (!samplingJob.Run()) {

    }

    // Converts from local space to model space matrices.
    ozz::animation::LocalToModelJob ltmJob;
    ltmJob.skeleton = skeleton_.get();
    ltmJob.input = make_span(locals);
    ltmJob.output = make_span(models);
    if (!ltmJob.Run()) {

    }
    
    const ozz::span<const int16_t>& parents = skeleton_->joint_parents();

    int instances = 0;
    for (int i = 0; i < numJoints; ++i)
    {
        // Root isn't rendered.
        const int16_t parentId = parents[i];
        if (parentId == ozz::animation::Skeleton::kNoParent) {
            continue;
        }

#if WIN32
        // Selects joint matrices.
        const ozz::math::Float4x4& parent = models[parentId];
        const ozz::math::Float4x4& current = models[i];

        // ozz::math::SimdFloat4 p0 = ozz::math::TransformPoint(parent, {0,0,0,1});
        // ozz::math::SimdFloat4 p1 = ozz::math::TransformPoint(current, {0,0,0,1});

        // glm::vec3 from = {p0.x, p0.y, p0.z};
        // glm::vec3 to = {p1.x, p1.y, p1.z};
        //
        // NextEngine::GetInstance()->DrawAuxLine(from, to, glm::vec4(1.0f, 1.0f, 1.0f, 1.0f), 1.0f);
        // NextEngine::GetInstance()->DrawAuxPoint(to, glm::vec4(1.0f, 0.0f, 0.0f, 1.0f), 2.0f);
#endif
    }
}

void NextAnimation::Stop()
{
    skeleton_.reset();
    animation_.reset();
    SContext.reset();
}
