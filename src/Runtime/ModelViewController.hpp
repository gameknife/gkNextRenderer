#pragma once

#include "FocusAnimation.hpp"
#include "Utilities/Glm.hpp"
#include <optional>

namespace Assets
{
    struct Camera;
}

union SDL_Event;

struct MovementInput
{
    float forward = 0.0f;   // [-1, 1]
    float right = 0.0f;     // [-1, 1]
    float up = 0.0f;        // [-1, 1]

    bool IsActive() const
    {
        return forward != 0.0f || right != 0.0f || up != 0.0f;
    }

    void Reset()
    {
        forward = right = up = 0.0f;
    }
};

class ModelViewController final
{
public:

	 void Reset(const Assets::Camera& RenderCamera);

	 glm::mat4 ModelView() const;
	 float FieldOfView() const { return fieldOfView_; }
	 glm::vec4 Position() const { return position_; }

	 bool OnKey(SDL_Event& event);
	 bool OnCursorPosition(double xpos, double ypos);
	 bool OnMouseButton(SDL_Event& event);
	 bool OnTouch(bool down, double xpos, double ypos);
	 void OnScroll(double xoffset, double yoffset);

	 bool OnGamepadInput(int16_t leftStickX, int16_t leftStickY,
	                    int16_t rightStickX, int16_t rightStickY,
	                    int16_t leftTrigger, int16_t rightTrigger);
	 bool UpdateCamera(double speed, double timeDelta);

	void SetModelRotation(double x, double y)
	{
		modelRotX_ = x;
		modelRotY_ = y;
	}

    void SetOrbitTarget(std::optional<glm::vec3> target) { orbitTarget_ = target; }
    void SetAltPressed(bool pressed) { altPressed_ = pressed; }
    void Focus(const glm::vec3& focusPoint, float radius = 0.5f);

	glm::vec3 GetRight() const;
	glm::vec3 GetUp() const;
	glm::vec3 GetForward() const;
	glm::vec3 GetPosition() const;

private:

	void MoveForward(float d);
	void MoveRight(float d);
	void MoveUp(float d);
	void Rotate(float y, float x);
    void Orbit(float x, float y);
	void UpdateVectors();

	// Matrices and vectors.
	glm::mat4 orientation_{};

	glm::vec4 position_{};
	glm::vec4 right_{ 1, 0, 0, 0 };
	glm::vec4 up_{ 0, 1, 0, 0 };
	glm::vec4 forward_{ 0, 0, -1, 0 };

    // Orbit control
    std::optional<glm::vec3> orbitTarget_;
    bool altPressed_{};

	// Focus animation
    FocusAnimation focusAnimation_;

    // Movement input (replaces 12 bools)
    MovementInput keyboardInput_;
    MovementInput gamepadInput_;

	// with smooth movement
	double cameraRotX_{};
	double cameraRotY_{};
    double cameraRotXAbs_{};
    double cameraRotYAbs_{};
	double modelRotX_{};
	double modelRotY_{};

	double rawCameraRotX_ {};
	double rawCameraRotY_ {};
	double rawModelRotX_ {};
	double rawModelRotY_ {};
	
	double mousePosX_{};
	double mousePosY_{};

	bool resetMousePos_{};
	bool mouseLeftPressed_{};
	bool mouseRightPressed_{};

	double mouseSensitive_ {};

	float fieldOfView_ {};

	bool movedByEvent_ {};
};
