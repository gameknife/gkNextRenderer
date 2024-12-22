#pragma once

#include "Utilities/Glm.hpp"

namespace Assets
{
	struct Camera;
}

class ModelViewController final
{
public:

	void Reset(const Assets::Camera& RenderCamera);

	glm::mat4 ModelView() const;
	float FieldOfView() const { return fieldOfView_; }
	glm::vec4 Position() const { return position_; }

	bool OnKey(int key, int scancode, int action, int mods);
	bool OnCursorPosition(double xpos, double ypos);
	bool OnMouseButton(int button, int action, int mods);
	bool OnTouch(bool down, double xpos, double ypos);
	void OnScroll(double xoffset, double yoffset);
	bool UpdateCamera(double speed, double timeDelta);

private:

	void MoveForward(float d);
	void MoveRight(float d);
	void MoveUp(float d);
	void Rotate(float y, float x);
	void UpdateVectors();

	// Matrices and vectors.
	glm::mat4 orientation_{};

	glm::vec4 position_{};
	glm::vec4 right_{ 1, 0, 0, 0 };
	glm::vec4 up_{ 0, 1, 0, 0 };
	glm::vec4 forward_{ 0, 0, -1, 0 };

	// Control states.
	bool cameraMovingLeft_{};
	bool cameraMovingRight_{};
	bool cameraMovingBackward_{};
	bool cameraMovingForward_{};
	bool cameraMovingDown_{};
	bool cameraMovingUp_{};

	// with smooth movement
	double cameraRotX_{};
	double cameraRotY_{};
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
};
