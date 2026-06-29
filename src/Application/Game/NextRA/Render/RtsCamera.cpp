#include "Render/RtsCamera.h"

#include "Engine/Assets/Core/Model.hpp"

#include <glm/gtc/matrix_transform.hpp>

namespace NextRA
{
    namespace
    {
        constexpr float minDistance = 8.0f;
        constexpr float maxDistance = 36.0f;
        constexpr float moveSpeed = 10.0f;
    }

    void FRtsCamera::Tick(double deltaSeconds)
    {
        glm::vec3 move(0.0f);
        if (moveForward_)
        {
            move.z -= 1.0f;
        }
        if (moveBack_)
        {
            move.z += 1.0f;
        }
        if (moveLeft_)
        {
            move.x -= 1.0f;
        }
        if (moveRight_)
        {
            move.x += 1.0f;
        }

        if (glm::length(move) > 0.001f)
        {
            focus_ += glm::normalize(move) * moveSpeed * static_cast<float>(deltaSeconds);
        }
    }

    void FRtsCamera::AddZoom(float steps)
    {
        distance_ = glm::clamp(distance_ - steps * 1.5f, minDistance, maxDistance);
    }

    bool FRtsCamera::OverrideRenderCamera(Assets::Camera& outRenderCamera) const
    {
        const glm::vec3 cameraPos = focus_ + glm::vec3(0.0f, distance_ * 0.9f, distance_ * 0.75f);
        outRenderCamera.ModelView = glm::lookAt(cameraPos, focus_, glm::vec3(0.0f, 1.0f, 0.0f));
        outRenderCamera.FieldOfView = 45.0f;
        return true;
    }
}
