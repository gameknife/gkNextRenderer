#include "ModelViewController.hpp"

#include "Assets/Model.hpp"
#include "Vulkan/Vulkan.hpp"

void ModelViewController::Reset(const Assets::Camera& RenderCamera)
{
    const auto inverse = glm::inverse(RenderCamera.ModelView);

    position_ = inverse * glm::vec4(0, 0, 0, 1);
    orientation_ = glm::mat4(glm::mat3(RenderCamera.ModelView));

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

    fieldOfView_ = RenderCamera.FieldOfView;

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

bool ModelViewController::OnKey(const int key, const int scancode, const int action, const int mods)
{
#if !ANDROID
    switch (key)
    {
    case GLFW_KEY_S: cameraMovingBackward_ = action != GLFW_RELEASE;
        return true;
    case GLFW_KEY_W: cameraMovingForward_ = action != GLFW_RELEASE;
        return true;
    case GLFW_KEY_A: cameraMovingLeft_ = action != GLFW_RELEASE;
        return true;
    case GLFW_KEY_D: cameraMovingRight_ = action != GLFW_RELEASE;
        return true;
    case GLFW_KEY_Q: cameraMovingDown_ = action != GLFW_RELEASE;
        return true;
    case GLFW_KEY_E: cameraMovingUp_ = action != GLFW_RELEASE;
        return true;
    default: return false;
    }
#else
	return false;
#endif
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

bool ModelViewController::OnMouseButton(const int button, const int action, const int mods)
{
#if !ANDROID
    if (button == GLFW_MOUSE_BUTTON_LEFT)
    {
        mouseLeftPressed_ = action == GLFW_PRESS;
        if (mouseLeftPressed_)
        {
            resetMousePos_ = true;
        }
    }

    if (button == GLFW_MOUSE_BUTTON_RIGHT)
    {
        mouseRightPressed_ = action == GLFW_PRESS;
        if (mouseRightPressed_)
        {
            resetMousePos_ = true;
        }
    }
#endif
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
        glm::abs(rawModelRotY_ - modelRotY_) > 0.01;

    cameraRotY_ = 0;
    cameraRotX_ = 0;

    return updated;
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
