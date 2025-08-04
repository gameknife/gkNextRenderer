#pragma once

#include "WindowConfig.hpp"
#include "Vulkan.hpp"
#include <functional>
#include <vector>

namespace Vulkan
{

	class Window final
	{
	public:

		VULKAN_NON_COPIABLE(Window)

		explicit Window(const WindowConfig& config);
		~Window();

		// Window instance properties.
		const WindowConfig& Config() const { return config_; }

		GLFWwindow* Handle() const { return window_; }

		float ContentScale() const;
		VkExtent2D FramebufferSize() const;
		VkExtent2D WindowSize() const;

		// GLFW instance properties (i.e. not bound to a window handler).
		const char* GetKeyName(int key, int scancode) const;
		std::vector<const char*> GetRequiredInstanceExtensions() const;
		double GetTime() const;

		// Callbacks
		std::function<void(int key, int scancode, int action, int mods)> OnKey;
		std::function<void(double xpos, double ypos)> OnCursorPosition;
		std::function<void(int button, int action, int mods)> OnMouseButton;
		std::function<void(double xoffset, double yoffset)> OnScroll;
		std::function<void(int path_count, const char* paths[])> OnDropFile;
		std::function<void(GLFWwindow* window, int focused)> OnFocus;

		// 添加手柄相关回调
		std::function<void(int jid, int event)> OnJoystickConnection;
		std::function<void(float leftStickX, float leftStickY,
						  float rightStickX, float rightStickY,
						  float leftTrigger, float rightTrigger)> OnGamepadInput;
  
		// 添加轮询手柄输入的方法
		void PollGamepadInput();
		
		// Methods
		void Close();
		bool IsMinimized() const;
		bool IsMaximumed() const;
		void WaitForEvents() const;
		void Show() const;

		void Minimize();
		void Maximum();
		void Restore();

		void attemptDragWindow();

		// Static methods
		static void InitGLFW();
		static void TerminateGLFW();
	private:

		const WindowConfig config_;
		GLFWwindow* window_{};

		double s_xpos = 0, s_ypos = 0;
		int w_xsiz = 0, w_ysiz = 0;
		int dragState = 0;
	};

}
