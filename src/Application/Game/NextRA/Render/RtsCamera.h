#pragma once

#include "Engine/Assets/AssetsFwd.hpp"
#include "Engine/Common/CoreMinimal.hpp"

#include <glm/vec3.hpp>

namespace NextRA
{
    class FRtsCamera
    {
    public:
        void Tick(double deltaSeconds);
        void SetMoveForward(bool active) { moveForward_ = active; }
        void SetMoveBack(bool active) { moveBack_ = active; }
        void SetMoveLeft(bool active) { moveLeft_ = active; }
        void SetMoveRight(bool active) { moveRight_ = active; }
        void AddZoom(float steps);
        bool OverrideRenderCamera(Assets::Camera& outRenderCamera) const;

    private:
        glm::vec3 focus_{0.0f, 0.0f, 0.0f};
        float distance_ = 18.0f;
        bool moveForward_ = false;
        bool moveBack_ = false;
        bool moveLeft_ = false;
        bool moveRight_ = false;
    };
}
