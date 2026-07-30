#include "Render/BattleCamera.h"

#include "Engine/Assets/Core/Model.hpp"
#include "Engine/Runtime/Components/TerrainComponent.hpp"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>

namespace NextTotalwar
{
    void FBattleCamera::Tick(float deltaSeconds, const Runtime::TerrainComponent* terrain)
    {
        if (rotateLeft_) yaw_ += deltaSeconds * 0.9f;
        if (rotateRight_) yaw_ -= deltaSeconds * 0.9f;

        glm::vec2 move(0.0f);
        if (moveForward_) move.y += 1.0f;
        if (moveBack_) move.y -= 1.0f;
        if (moveLeft_) move.x -= 1.0f;
        if (moveRight_) move.x += 1.0f;
        if (glm::length(move) > 0.01f)
        {
            move = glm::normalize(move);
            const glm::vec3 forward(-std::sin(yaw_), 0.0f, -std::cos(yaw_));
            const glm::vec3 right(std::cos(yaw_), 0.0f, -std::sin(yaw_));
            const glm::vec3 movement =
                (right * move.x + forward * move.y) *
                deltaSeconds * (18.0f + distance_ * 0.22f);
            if (following_) followOffset_ += movement;
            else focus_ += movement;
        }
        if (following_)
        {
            focus_.x = followTarget_.x + followOffset_.x;
            focus_.z = followTarget_.z + followOffset_.z;
        }
        focus_.x = glm::clamp(focus_.x, -190.0f, 190.0f);
        focus_.z = glm::clamp(focus_.z, -190.0f, 190.0f);
        const float height = terrain ? terrain->SampleHeight(focus_.x, focus_.z) : 0.0f;
        focus_.y = glm::mix(focus_.y, height, 1.0f - std::exp(-deltaSeconds * 7.0f));
    }

    void FBattleCamera::PanByScreenDelta(const glm::vec2& delta)
    {
        const glm::vec3 forward(-std::sin(yaw_), 0.0f, -std::cos(yaw_));
        const glm::vec3 right(std::cos(yaw_), 0.0f, -std::sin(yaw_));
        const glm::vec3 movement =
            (-right * delta.x + forward * delta.y) * (distance_ * 0.0022f);
        if (following_) followOffset_ += movement;
        else focus_ += movement;
    }

    void FBattleCamera::SetFollowTarget(const glm::vec3& target, bool centerImmediately)
    {
        if (!following_ || centerImmediately)
        {
            followOffset_ = {};
        }
        following_ = true;
        followTarget_ = target;
        if (centerImmediately)
        {
            focus_ = target;
        }
    }

    void FBattleCamera::ClearFollowTarget()
    {
        following_ = false;
        followOffset_ = {};
    }

    void FBattleCamera::AddZoom(float steps)
    {
        distance_ = glm::clamp(distance_ - steps * 10.0f, 28.0f, 230.0f);
    }

    bool FBattleCamera::OverrideRenderCamera(Assets::Camera& camera) const
    {
        const glm::vec3 direction(std::sin(yaw_), 0.0f, std::cos(yaw_));
        const glm::vec3 position = focus_ + direction * distance_ * 0.70f +
                                   glm::vec3(0.0f, distance_ * 0.82f, 0.0f);
        camera.ModelView = glm::lookAt(position, focus_, glm::vec3(0.0f, 1.0f, 0.0f));
        camera.FieldOfView = 48.0f;
        camera.NearPlane = 0.1f;
        camera.FarPlane = 900.0f;
        return true;
    }
}
