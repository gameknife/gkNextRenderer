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

		Next_Window* Handle() const { return window_; }

		float ContentScale() const;
		VkExtent2D FramebufferSize() const;
		VkExtent2D WindowSize() const;

		// GLFW instance properties (i.e. not bound to a window handler).
		std::vector<const char*> GetRequiredInstanceExtensions() const;
		double GetTime() const;

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
		Next_Window* window_{};

		double s_xpos = 0, s_ypos = 0;
		int w_xsiz = 0, w_ysiz = 0;
		int dragState = 0;
	};

}
