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

    if (mouseLeftPressed_)
    {
        cameraRotXAbs_ += deltaX;
        cameraRotYAbs_ += deltaY;
    }

    if (mouseRightPressed_)
    {
        rawModelRotX_ += deltaX * 500.0;
        rawModelRotY_ += deltaY * 500.0;
    }

    mousePosX_ = xpos;
    mousePosY_ = ypos;

    return mouseLeftPressed_ || mouseRightPressed_;
}

bool ModelViewController::OnMouseButton(SDL_Event& event)
{
    if (event.button.button == SDL_BUTTON_LEFT)
    {
        mouseLeftPressed_ = event.button.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
        if (mouseLeftPressed_)
        {
            resetMousePos_ = true;
        }
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
