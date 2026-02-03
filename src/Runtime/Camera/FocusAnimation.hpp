#pragma once

#include "Utilities/Glm.hpp"

class FocusAnimation
{
public:
    void Start(glm::vec3 startPos, glm::quat startRot,
               glm::vec3 targetPos, glm::quat targetRot);

    // Returns true if animation is still active
    bool Update(float deltaTime, glm::vec3& outPos, glm::quat& outRot);

    bool IsActive() const { return isActive_; }
    void Cancel() { isActive_ = false; }

private:
    static float SmoothStep(float t)
    {
        return t * t * (3.0f - 2.0f * t);
    }

    bool isActive_ = false;
    float timer_ = 0.0f;
    static constexpr float DURATION = 0.5f;

    glm::vec3 startPos_{};
    glm::vec3 targetPos_{};
    glm::quat startRot_{};
    glm::quat targetRot_{};
};
