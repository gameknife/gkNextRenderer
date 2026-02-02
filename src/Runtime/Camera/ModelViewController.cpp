#include "Runtime/Camera/ModelViewController.hpp"
#include "Assets/Model.hpp"
#include "Vulkan/Vulkan.hpp"
#include "Platform/PlatformCommon.h"

void ModelViewController::Reset(const Assets::Camera& renderCamera)
{
    const auto inverse = glm::inverse(renderCamera.ModelView);

    position_ = inverse * glm::vec4(0, 0, 0, 1);
    orientation_ = glm::mat4(glm::mat3(renderCamera.ModelView));

    cameraRotX_ = 0;
    cameraRotY_ = 0;
    cameraRotXAbs_ = 0;
    cameraRotYAbs_ = 0;
    
    modelRotX_ = 0;
    modelRotY_ = 0;
    rawModelRotX_ = 0;
    rawModelRotY_ = 0;
    rawCameraRotX_ = 0;
    rawCameraRotY_ = 0;

    mouseLeftPressed_ = false;
    mouseRightPressed_ = false;

    mouseSensitive_ = 0.0002;

    fieldOfView_ = renderCamera.FieldOfView;

    UpdateVectors();
}

glm::mat4 ModelViewController::ModelView() const
{
    const auto cameraRotY = static_cast<float>(modelRotX_ / 300.0);

    const auto model =
        glm::rotate(glm::mat4(1.0f), cameraRotY * glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f));

    const auto view = orientation_ * glm::translate(glm::mat4(1), -glm::vec3(position_));

    return view * model;
}

bool ModelViewController::OnKey(SDL_Event& event)
{
    // WASDQE movement only when right mouse button is pressed (like Unreal Engine)
    if (!mouseRightPressed_)
    {
        return false;
    }

    // Any manual input cancels focus animation
    if (focusAnimation_.IsActive())
    {
        focusAnimation_.Cancel();
    }

    const bool isDown = event.key.type != SDL_EVENT_KEY_UP;
    const float value = isDown ? 1.0f : 0.0f;

    switch (event.key.key)
    {
    case SDLK_W:
        keyboardInput_.forward = value;
        return true;
    case SDLK_S:
        keyboardInput_.forward = isDown ? -1.0f : 0.0f;
        return true;
    case SDLK_D:
        keyboardInput_.right = value;
        return true;
    case SDLK_A:
        keyboardInput_.right = isDown ? -1.0f : 0.0f;
        return true;
    case SDLK_Q:
        keyboardInput_.up = value;
        return true;
    case SDLK_E:
        keyboardInput_.up = isDown ? -1.0f : 0.0f;
        return true;
    default:
        return false;
    }
}

bool ModelViewController::OnGamepadInput(const int16_t leftStickX, const int16_t leftStickY,
                                        const int16_t rightStickX, const int16_t rightStickY,
                                        const int16_t leftTrigger, const int16_t rightTrigger)
{
    constexpr float STICK_SENSITIVITY = 1.0f / 32767.0f;
    constexpr int16_t DEAD_ZONE = 3000;
    constexpr double STICK_THRESHOLD = 0.7;

    bool inputDetected = false;

    // Cancel focus on significant input
    if (std::abs(leftStickX) > DEAD_ZONE || std::abs(leftStickY) > DEAD_ZONE ||
        std::abs(rightStickX) > DEAD_ZONE || std::abs(rightStickY) > DEAD_ZONE ||
        leftTrigger > DEAD_ZONE || rightTrigger > DEAD_ZONE)
    {
        focusAnimation_.Cancel();
    }

    // Left stick: movement
    gamepadInput_.right = std::abs(leftStickX) > DEAD_ZONE
        ? leftStickX * STICK_SENSITIVITY : 0.0f;
    gamepadInput_.forward = std::abs(leftStickY) > DEAD_ZONE
        ? -leftStickY * STICK_SENSITIVITY : 0.0f;  // Y is inverted

    // Triggers: up/down
    float upInput = rightTrigger > DEAD_ZONE ? rightTrigger * STICK_SENSITIVITY : 0.0f;
    float downInput = leftTrigger > DEAD_ZONE ? leftTrigger * STICK_SENSITIVITY : 0.0f;
    gamepadInput_.up = upInput - downInput;

    inputDetected = gamepadInput_.IsActive();

    // Right stick: rotation
    if (std::abs(rightStickX) > DEAD_ZONE || std::abs(rightStickY) > DEAD_ZONE)
    {
        cameraRotX_ = rightStickX * STICK_SENSITIVITY;
        cameraRotX_ = glm::sign(cameraRotX_) * glm::min(STICK_THRESHOLD, cameraRotX_ * cameraRotX_);
        cameraRotY_ = rightStickY * STICK_SENSITIVITY;
        cameraRotY_ = glm::sign(cameraRotY_) * glm::min(STICK_THRESHOLD, cameraRotY_ * cameraRotY_);
        inputDetected = true;
    }

    return inputDetected;
}

bool ModelViewController::OnCursorPosition(const double xpos, const double ypos)
{
    if (resetMousePos_)
    {
        resetMousePos_ = false;
        mousePosX_ = xpos;
        mousePosY_ = ypos;
    }
    
    const auto deltaX = static_cast<float>(xpos - mousePosX_) * mouseSensitive_;
    const auto deltaY = static_cast<float>(ypos - mousePosY_) * mouseSensitive_;

    // Mouse Right Button Handling
    if (mouseRightPressed_)
    {
        // Cancel focus on manual rotation
        focusAnimation_.Cancel();

        if (altPressed_ && orbitTarget_.has_value())
        {
            // Orbit Mode
            Orbit(deltaX, deltaY);
        }
        else
        {
            // Free Look Mode (was on Left Button)
            cameraRotXAbs_ += deltaX;
            cameraRotYAbs_ += deltaY;
        }
    }

    // Mouse Left Button is now ignored for camera control.

    mousePosX_ = xpos;
    mousePosY_ = ypos;

    return mouseLeftPressed_ || mouseRightPressed_;
}

bool ModelViewController::OnMouseButton(SDL_Event& event)
{
    if (event.button.button == SDL_BUTTON_LEFT)
    {
        mouseLeftPressed_ = event.button.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
    }

    if (event.button.button == SDL_BUTTON_RIGHT)
    {
        mouseRightPressed_ = event.button.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
        if (mouseRightPressed_)
        {
            resetMousePos_ = true;
        }
        else
        {
            // Reset keyboard movement when right mouse is released
            keyboardInput_.Reset();
        }
    }
    return true;
}

void ModelViewController::Focus(const glm::vec3& focusPoint, float radius)
{
    // Calculate distance to fit the object in view
    float fovRad = glm::radians(fieldOfView_);
    float dist = radius / glm::sin(fovRad * 0.5f) * 1.1f;

    // Target position: back from focus point along forward vector
    glm::vec3 fwd = GetForward();
    glm::vec3 targetPos = focusPoint - fwd * dist;

    // Start animation (keep current rotation)
    glm::quat currentRot = glm::quat_cast(orientation_);
    focusAnimation_.Start(
        glm::vec3(position_),
        currentRot,
        targetPos,
        currentRot
    );
}

void ModelViewController::Orbit(float deltaX, float deltaY)
{
    if (!orbitTarget_.has_value())
    {
        return;
    }

    glm::vec3 target = orbitTarget_.value();

    // Yaw around world Y, Pitch around camera right
    glm::mat4 yaw = glm::rotate(glm::mat4(1.0f), -deltaX * 5.0f, glm::vec3(0, 1, 0));
    glm::mat4 pitch = glm::rotate(glm::mat4(1.0f), -deltaY * 5.0f, glm::vec3(GetRight()));
    glm::mat4 rotation = yaw * pitch;

    // Rotate position around target
    glm::vec3 arm = glm::vec3(position_) - target;
    position_ = glm::vec4(target + glm::vec3(rotation * glm::vec4(arm, 0.0f)), 1.0f);

    // Update orientation to maintain view direction
    orientation_ = orientation_ * glm::transpose(rotation);

    UpdateVectors();
}

bool ModelViewController::OnTouch(bool down, double xpos, double ypos)
{
    mouseRightPressed_ = down;

    mousePosX_ = xpos;
    mousePosY_ = ypos;

    return true;
}

void ModelViewController::OnScroll(double xoffset, double yoffset)
{
    // Cancel focus on scroll
    focusAnimation_.Cancel();

    const float scrollSpeed = 0.5f;
    MoveForward(static_cast<float>(yoffset) * scrollSpeed);
    movedByEvent_ = true;
}

bool ModelViewController::UpdateCamera(const double speed, const double timeDelta)
{
    // Handle focus animation
    glm::vec3 focusPos;
    glm::quat focusRot;
    if (focusAnimation_.Update(static_cast<float>(timeDelta), focusPos, focusRot))
    {
        position_ = glm::vec4(focusPos, 1.0f);
        orientation_ = glm::mat4(glm::mat3(focusRot));
        UpdateVectors();
        return true;
    }

    const auto d = static_cast<float>(speed * timeDelta);

    // Combine keyboard and gamepad input
    float totalRight = keyboardInput_.right + gamepadInput_.right;
    float totalForward = keyboardInput_.forward + gamepadInput_.forward;
    float totalUp = keyboardInput_.up + gamepadInput_.up;

    if (totalRight != 0.0f)
    {
        MoveRight(d * totalRight);
    }
    if (totalForward != 0.0f)
    {
        MoveForward(d * totalForward);
    }
    if (totalUp != 0.0f)
    {
        MoveUp(d * totalUp);
    }

    modelRotX_ = glm::mix(modelRotX_, rawModelRotX_, 0.5);
    modelRotY_ = glm::mix(modelRotY_, rawModelRotY_, 0.5);

    const double rotationDiv = 1 / timeDelta;
    Rotate(static_cast<float>(cameraRotX_ / rotationDiv + cameraRotXAbs_), static_cast<float>(cameraRotY_ / rotationDiv + cameraRotYAbs_));

    const bool hasMovement = keyboardInput_.IsActive() || gamepadInput_.IsActive();
    const bool updated =
        hasMovement ||
        (cameraRotY_ + cameraRotYAbs_) != 0.0 ||
        (cameraRotX_ + cameraRotXAbs_) != 0.0 ||
        glm::abs(rawModelRotX_ - modelRotX_) > 0.01 ||
        glm::abs(rawModelRotY_ - modelRotY_) > 0.01 ||
        movedByEvent_;

    cameraRotY_ = 0;
    cameraRotX_ = 0;
    cameraRotXAbs_ = 0;
    cameraRotYAbs_ = 0;
    movedByEvent_ = false;

    return updated;
}

glm::vec3 ModelViewController::GetRight() const
{
    return glm::vec3(right_);
}

glm::vec3 ModelViewController::GetUp() const
{
    return glm::vec3(up_);
}

glm::vec3 ModelViewController::GetForward() const
{
    return glm::vec3(forward_);
}

glm::vec3 ModelViewController::GetPosition() const
{
    return glm::vec3(position_);
}

void ModelViewController::MoveForward(const float d)
{
    position_ += d * forward_;
}

void ModelViewController::MoveRight(const float d)
{
    position_ += d * right_;
}

void ModelViewController::MoveUp(const float d)
{
    position_ += d * up_;
}

void ModelViewController::Rotate(const float y, const float x)
{
    orientation_ =
        glm::rotate(glm::mat4(1), x, glm::vec3(1, 0, 0)) *
        orientation_ *
        glm::rotate(glm::mat4(1), y, glm::vec3(0, 1, 0));

    UpdateVectors();
}

void ModelViewController::UpdateVectors()
{
    // Given the ortientation matrix, find out the x,y,z vector orientation.
    const auto inverse = glm::inverse(orientation_);

    right_ = inverse * glm::vec4(1, 0, 0, 0);
    up_ = inverse * glm::vec4(0, 1, 0, 0);
    forward_ = inverse * glm::vec4(0, 0, -1, 0);
}
