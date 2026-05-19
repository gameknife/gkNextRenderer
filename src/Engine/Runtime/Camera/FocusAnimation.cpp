#include "Engine/Runtime/Camera/FocusAnimation.hpp"

namespace Runtime::Camera
{

void FocusAnimation::Start(glm::vec3 startPos, glm::quat startRot,
                           glm::vec3 targetPos, glm::quat targetRot)
{
    isActive_ = true;
    timer_ = 0.0f;
    startPos_ = startPos;
    targetPos_ = targetPos;
    startRot_ = startRot;
    targetRot_ = targetRot;
}

bool FocusAnimation::Update(float deltaTime, glm::vec3& outPos, glm::quat& outRot)
{
    if (!isActive_)
    {
        return false;
    }

    timer_ += deltaTime;
    float t = glm::clamp(timer_ / DURATION, 0.0f, 1.0f);
    t = SmoothStep(t);

    outPos = glm::mix(startPos_, targetPos_, t);
    outRot = glm::slerp(startRot_, targetRot_, t);

    if (t >= 1.0f)
    {
        isActive_ = false;
    }

    return true;
}

}
