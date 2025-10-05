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
    modelRotX_ = 0;
    modelRotY_ = 0;
    rawModelRotX_ = 0;
    rawModelRotY_ = 0;
    rawCameraRotX_ = 0;
    rawCameraRotY_ = 0;

    mouseLeftPressed_ = false;
    mouseRightPressed_ = false;

    mouseSensitive_ = 0.5;

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
    case SDLK_S: cameraMovingBackward_ = event.key.type != SDL_EVENT_KEY_UP;
        return true;
    case SDLK_W: cameraMovingForward_ =event.key.type != SDL_EVENT_KEY_UP;
        return true;
    case SDLK_A: cameraMovingLeft_ = event.key.type != SDL_EVENT_KEY_UP;
        return true;
    case SDLK_D: cameraMovingRight_ = event.key.type != SDL_EVENT_KEY_UP;
        return true;
    case SDLK_Q: cameraMovingDown_ = event.key.type != SDL_EVENT_KEY_UP;
        return true;
    case SDLK_E: cameraMovingUp_ = event.key.type != SDL_EVENT_KEY_UP;
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
    const float rightStickSensitivity = 0.0001f;
    const int16_t deadZone = 5000; // 摇杆死区
    bool inputDetected = false;
    
    // 左摇杆控制前后左右移动
    if (std::abs(leftStickX) > deadZone) {
        cameraMovingRight_ = leftStickX > 0;
        cameraMovingLeft_ = leftStickX < 0;
        inputDetected = true;
    }
    else {
        cameraMovingRight_ = false;
        cameraMovingLeft_ = false;
    }
    
    if (std::abs(leftStickY) > deadZone) {
        cameraMovingForward_ = leftStickY < 0;
        cameraMovingBackward_ = leftStickY > 0;
        inputDetected = true;
    }
    else {
        cameraMovingForward_ = false;
        cameraMovingBackward_ = false;
    }
    
    // 扳机键控制上下移动
    if (leftTrigger > deadZone) {
        cameraMovingDown_ = true;
        inputDetected = true;
    }
    else {
        cameraMovingDown_ = false;
    }
    
    if (rightTrigger > deadZone) {
        cameraMovingUp_ = true;
        inputDetected = true;
    }
    else {
        cameraMovingUp_ = false;
    }
    
    // 右摇杆可以用于视角旋转
    if (std::abs(rightStickX) > deadZone || std::abs(rightStickY) > deadZone) {
        cameraRotX_ = rightStickX * rightStickSensitivity;  // 根据需要调整灵敏度
        cameraRotY_ = rightStickY * rightStickSensitivity;
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

#if ANDROID
    const auto deltaY = static_cast<float>(xpos - mousePosX_) * mouseSensitive_ * -1;
    const auto deltaX = static_cast<float>(ypos - mousePosY_) * mouseSensitive_;
#else
    const auto deltaX = static_cast<float>(xpos - mousePosX_) * mouseSensitive_;
    const auto deltaY = static_cast<float>(ypos - mousePosY_) * mouseSensitive_;
#endif

    if (mouseLeftPressed_)
    {
        cameraRotX_ += deltaX;
        cameraRotY_ += deltaY;
    }

    if (mouseRightPressed_)
    {
        rawModelRotX_ += deltaX;
        rawModelRotY_ += deltaY;
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

    if (cameraMovingLeft_) MoveRight(-d);
    if (cameraMovingRight_) MoveRight(d);
    if (cameraMovingBackward_) MoveForward(-d);
    if (cameraMovingForward_) MoveForward(d);
    if (cameraMovingDown_) MoveUp(-d);
    if (cameraMovingUp_) MoveUp(d);

    modelRotX_ = glm::mix(modelRotX_, rawModelRotX_, 0.5);
    modelRotY_ = glm::mix(modelRotY_, rawModelRotY_, 0.5);

    const double rotationDiv = 1200;
    Rotate(static_cast<float>(cameraRotX_ / rotationDiv), static_cast<float>(cameraRotY_ / rotationDiv));

    const bool updated =
        cameraMovingLeft_ ||
        cameraMovingRight_ ||
        cameraMovingBackward_ ||
        cameraMovingForward_ ||
        cameraMovingDown_ ||
        cameraMovingUp_ ||
        cameraRotY_ != 0.0 ||
        cameraRotX_ != 0.0 ||
        glm::abs(rawModelRotX_ - modelRotX_) > 0.01 ||
        glm::abs(rawModelRotY_ - modelRotY_) > 0.01 || movedByEvent_;;

    cameraRotY_ = 0;
    cameraRotX_ = 0;
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
