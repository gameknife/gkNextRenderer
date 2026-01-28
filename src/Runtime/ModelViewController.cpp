#include "ModelViewController.hpp"
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
    // Any manual input cancels focus animation
    if (isFocusing_) isFocusing_ = false;

    switch (event.key.key)
    {
    case SDLK_S: cameraMovingBackward_ = event.key.type != SDL_EVENT_KEY_UP; cameraMovingSpeed_ = {1.0f, 1.0f};
        return true;
    case SDLK_W: cameraMovingForward_ =event.key.type != SDL_EVENT_KEY_UP; cameraMovingSpeed_ = {1.0f, 1.0f};
        return true;
    case SDLK_A: cameraMovingLeft_ = event.key.type != SDL_EVENT_KEY_UP; cameraMovingSpeed_ = {1.0f, 1.0f};
        return true;
    case SDLK_D: cameraMovingRight_ = event.key.type != SDL_EVENT_KEY_UP; cameraMovingSpeed_ = {1.0f, 1.0f};
        return true;
    case SDLK_Q: cameraMovingDown_ = event.key.type != SDL_EVENT_KEY_UP; cameraMovingSpeed_ = {1.0f, 1.0f};
        return true;
    case SDLK_E: cameraMovingUp_ = event.key.type != SDL_EVENT_KEY_UP; cameraMovingSpeed_ = {1.0f, 1.0f};
        return true;
    default: return false;
    }
    return false;
}

// 新增手柄输入处理函数
bool ModelViewController::OnGamepadInput(const int16_t leftStickX, const int16_t leftStickY,
                                        const int16_t rightStickX, const int16_t rightStickY,
                                        const int16_t leftTrigger, const int16_t rightTrigger)
{
    const float stickSensitivity = 0.0000352f; // 1 / 32767
    const int16_t deadZone = 3000; // 摇杆死区
    const double stickThreshold = 0.7; // 摇杆阈值
    bool inputDetected = false;
    
    // Any manual input cancels focus animation
    if (std::abs(leftStickX) > deadZone || std::abs(leftStickY) > deadZone || 
        std::abs(rightStickX) > deadZone || std::abs(rightStickY) > deadZone ||
        leftTrigger > deadZone || rightTrigger > deadZone)
    {
        isFocusing_ = false;
    }

    // 左摇杆控制前后左右移动
    if (std::abs(leftStickX) > deadZone) {
        cameraMovingRightJoystick_ = leftStickX > 0;
        cameraMovingLeftJoystick_ = leftStickX < 0;
        cameraMovingSpeed_.x = std::abs(leftStickX) * stickSensitivity;
        inputDetected = true;
    }
    else {
        cameraMovingRightJoystick_ = false;
        cameraMovingLeftJoystick_ = false;
        cameraMovingSpeed_.x = 1.0f;
    }
    
    if (std::abs(leftStickY) > deadZone) {
        cameraMovingForwardJoystick_ = leftStickY < 0;
        cameraMovingBackwardJoystick_ = leftStickY > 0;
        cameraMovingSpeed_.y = std::abs(leftStickY) * stickSensitivity;
        inputDetected = true;
    }
    else {
        cameraMovingForwardJoystick_ = false;
        cameraMovingBackwardJoystick_ = false;
        cameraMovingSpeed_.y = 1.0f;
    }
    
    // 扳机键控制上下移动
    if (leftTrigger > deadZone) {
        cameraMovingDownJoystick_ = true;
        inputDetected = true;
    }
    else {
        cameraMovingDownJoystick_ = false;
    }
    
    if (rightTrigger > deadZone) {
        cameraMovingUpJoystick_ = true;
        inputDetected = true;
    }
    else {
        cameraMovingUpJoystick_ = false;
    }
    
    // 右摇杆可以用于视角旋转
    if (std::abs(rightStickX) > deadZone || std::abs(rightStickY) > deadZone) {
        cameraRotX_ = rightStickX * stickSensitivity;  // 根据需要调整灵敏度
        cameraRotX_ = glm::sign(cameraRotX_) * glm::min(stickThreshold, cameraRotX_ * cameraRotX_);
        cameraRotY_ = rightStickY * stickSensitivity;
        cameraRotY_ = glm::sign(cameraRotY_) * glm::min(stickThreshold, cameraRotY_ * cameraRotY_);
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
        isFocusing_ = false;

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
        // Left button does not trigger camera reset or movement anymore
    }
    
    if (event.button.button == SDL_BUTTON_RIGHT)
    {
        mouseRightPressed_ = event.button.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
        if (mouseRightPressed_)
        {
            resetMousePos_ = true;
        }
    }
    return true;
}

void ModelViewController::Focus(const glm::vec3& focusPoint, float radius)
{
    // Move camera to look at the object from a fixed distance based on radius
    
    // Calculate distance to fit the object
    // half_size = distance * tan(fov/2)
    // distance = half_size / tan(fov/2)
    
    float fovRad = glm::radians(fieldOfView_);
    float dist = radius / glm::sin(fovRad * 0.5f);
    
    // Add a little margin
    dist *= 1.1f;
    
    // Preserve current viewing angle:
    // Move the camera to a position such that it looks at 'focusPoint'
    // with the SAME orientation it currently has.
    // This means moving along the backward vector of the camera.
    
    glm::vec3 fwd = GetForward(); 
    glm::vec3 currentPos = glm::vec3(position_);
    
    // Target position is simply back from the focus point along the forward vector
    glm::vec3 targetPos = focusPoint - fwd * dist;

    // Setup interpolation
    isFocusing_ = true;
    focusTimer_ = 0.0f;
    
    focusStartPos_ = currentPos;
    focusTargetPos_ = targetPos;
    
    focusStartRot_ = glm::quat_cast(orientation_);
    focusTargetRot_ = focusStartRot_; // Keep rotation constant
}

void ModelViewController::Orbit(float deltaX, float deltaY)
{
    if (!orbitTarget_.has_value()) return;

    glm::vec3 target = orbitTarget_.value();
    glm::vec3 camPos = glm::vec3(position_);
    
    // Calculate Rotation Matrices
    // Yaw (World Y)
    // Drag Right (deltaX > 0) -> Rotate Camera Left (CW around Y) -> Negative Angle
    glm::mat4 yaw = glm::rotate(glm::mat4(1.0f), -deltaX * 5.0f, glm::vec3(0, 1, 0));
    
    // Pitch (Camera Right)
    // Drag Down (deltaY > 0) -> Rotate Camera Up (CW around Right?) 
    // Right(1,0,0). Z(0,0,1). Y(0,1,0).
    // +90 around X: Z -> -Y (Down). 
    // -90 around X: Z -> Y (Up).
    // So Negative Angle for Drag Down.
    glm::mat4 pitch = glm::rotate(glm::mat4(1.0f), -deltaY * 5.0f, glm::vec3(GetRight()));
    
    glm::mat4 R = yaw * pitch;

    // Update Position
    // R rotates the Arm vector in World Space
    glm::vec3 arm = glm::vec3(position_) - target;
    position_ = glm::vec4(target + glm::vec3(R * glm::vec4(arm, 0.0f)), 1.0f);

    // Update Orientation
    // The Camera Frame must also rotate by R in World Space to maintain relative alignment.
    // C2W_new = R * C2W_old
    // W2C_new = inv(C2W_new) = inv(R * C2W_old) = W2C_old * inv(R)
    // Since R is a rotation matrix, inv(R) == transpose(R)
    orientation_ = orientation_ * glm::transpose(R);
    
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
    fieldOfView_ -= static_cast<float>(yoffset);
    fieldOfView_ = glm::clamp(fieldOfView_, 1.0f, 90.0f);
    movedByEvent_ = true;
}

bool ModelViewController::UpdateCamera(const double speed, const double timeDelta)
{
    if (isFocusing_)
    {
        focusTimer_ += static_cast<float>(timeDelta);
        float t = glm::clamp(focusTimer_ / focusDuration_, 0.0f, 1.0f);
        
        // Smooth step
        t = t * t * (3.0f - 2.0f * t);
        
        position_ = glm::vec4(glm::mix(focusStartPos_, focusTargetPos_, t), 1.0f);
        glm::quat currentRot = glm::slerp(focusStartRot_, focusTargetRot_, t);
        orientation_ = glm::mat4(glm::mat3(currentRot));
        
        UpdateVectors();
        
        if (t >= 1.0f)
        {
            isFocusing_ = false;
        }
        
        return true;
    }

    const auto d = static_cast<float>(speed * timeDelta);

    if (cameraMovingLeft_ || cameraMovingLeftJoystick_) MoveRight(-d * cameraMovingSpeed_.x);
    if (cameraMovingRight_ || cameraMovingRightJoystick_) MoveRight(d * cameraMovingSpeed_.x);
    if (cameraMovingBackward_ || cameraMovingBackwardJoystick_) MoveForward(-d * cameraMovingSpeed_.y);
    if (cameraMovingForward_ || cameraMovingForwardJoystick_) MoveForward(d * cameraMovingSpeed_.y);
    if (cameraMovingDown_ || cameraMovingDownJoystick_) MoveUp(-d);
    if (cameraMovingUp_ || cameraMovingUpJoystick_) MoveUp(d);

    modelRotX_ = glm::mix(modelRotX_, rawModelRotX_, 0.5);
    modelRotY_ = glm::mix(modelRotY_, rawModelRotY_, 0.5);

    const double rotationDiv = 1 / timeDelta;
    Rotate(static_cast<float>(cameraRotX_ / rotationDiv + cameraRotXAbs_), static_cast<float>(cameraRotY_ / rotationDiv + cameraRotYAbs_));

    const bool updated =
        cameraMovingLeft_ || cameraMovingLeftJoystick_ ||
        cameraMovingRight_ || cameraMovingRightJoystick_ ||
        cameraMovingBackward_ || cameraMovingBackwardJoystick_ ||
        cameraMovingForward_ || cameraMovingForwardJoystick_ ||
        cameraMovingDown_ || cameraMovingDownJoystick_ ||
        cameraMovingUp_ || cameraMovingUpJoystick_ ||
        (cameraRotY_ + cameraRotYAbs_) != 0.0 ||
        (cameraRotX_ + cameraRotXAbs_) != 0.0 ||
        glm::abs(rawModelRotX_ - modelRotX_) > 0.01 ||
        glm::abs(rawModelRotY_ - modelRotY_) > 0.01 || movedByEvent_;;

    cameraRotY_ = 0;
    cameraRotX_ = 0;
    cameraRotXAbs_ = 0;
    cameraRotYAbs_ = 0;
    movedByEvent_ = false;
    
    return updated;
}

glm::vec3 ModelViewController::GetRight()
{
    glm::mat4 mvi = inverse(ModelView());
    glm::vec4 origin = mvi * glm::vec4(1, 0, 0,0);
    return origin;
}

glm::vec3 ModelViewController::GetUp()
{
    glm::mat4 mvi = inverse(ModelView());
    glm::vec4 origin = mvi * glm::vec4(0, 1, 0,0);
    return origin;
}

glm::vec3 ModelViewController::GetForward()
{
    glm::mat4 mvi = inverse(ModelView());
    glm::vec4 origin = mvi * glm::vec4(0, 0, -1, 0);
    return origin;
}

glm::vec3 ModelViewController::GetPosition()
{
    glm::mat4 mvi = inverse(ModelView());
    glm::vec4 origin = mvi * glm::vec4(0, 0, 0, 1);
    return origin;
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
