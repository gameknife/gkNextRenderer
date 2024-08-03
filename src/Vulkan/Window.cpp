#include "Window.hpp"
#include "Utilities/Exception.hpp"
#include "Utilities/StbImage.hpp"
#include <fmt/format.h>

#if ANDROID
#include <time.h>

static double now_ms(void) {

    struct timespec res;
    clock_gettime(CLOCK_REALTIME, &res);
    return 1000.0 * res.tv_sec + (double) res.tv_nsec / 1e6;
}
#endif

namespace Vulkan {

namespace
{
#if !ANDROID
	void GlfwErrorCallback(const int error, const char* const description)
	{
		fmt::print(stderr, "ERROR: GLFW: {} (code: {})\n", description, error);
	}

	void GlfwKeyCallback(GLFWwindow* window, const int key, const int scancode, const int action, const int mods)
	{
		auto* const this_ = static_cast<Window*>(glfwGetWindowUserPointer(window));
		if (this_->OnKey)
		{
			this_->OnKey(key, scancode, action, mods);
		}
	}

	void GlfwCursorPositionCallback(GLFWwindow* window, const double xpos, const double ypos)
	{
		auto* const this_ = static_cast<Window*>(glfwGetWindowUserPointer(window));
		if (this_->OnCursorPosition)
		{
			this_->OnCursorPosition(xpos, ypos);
		}
	}

	void GlfwMouseButtonCallback(GLFWwindow* window, const int button, const int action, const int mods)
	{
		auto* const this_ = static_cast<Window*>(glfwGetWindowUserPointer(window));
		if (this_->OnMouseButton)
		{
			this_->OnMouseButton(button, action, mods);
		}
	}

	void GlfwScrollCallback(GLFWwindow* window, const double xoffset, const double yoffset)
	{
		auto* const this_ = static_cast<Window*>(glfwGetWindowUserPointer(window));
		if (this_->OnScroll)
		{
			this_->OnScroll(xoffset, yoffset);
		}
	}
	void GlfwSetDropCallback(GLFWwindow* window, int path_count, const char* paths[])
	{
		auto* const this_ = static_cast<Window*>(glfwGetWindowUserPointer(window));
		if (this_->OnDropFile)
		{
			this_->OnDropFile(path_count, paths);
		}
	}
#endif
}

Window::Window(const WindowConfig& config) :
	config_(config)
{
#if !ANDROID
	glfwSetErrorCallback(GlfwErrorCallback);

	if (!glfwInit())
	{
		Throw(std::runtime_error("glfwInit() failed"));
	}

	if (!glfwVulkanSupported())
	{
		Throw(std::runtime_error("glfwVulkanSupported() failed"));
	}

	// hide title bar, handle in ImGUI Later
	glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_RESIZABLE, config.Resizable ? GLFW_TRUE : GLFW_FALSE);

	auto* const monitor = config.Fullscreen ? glfwGetPrimaryMonitor() : nullptr;

	// create with hidden window
	glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
	window_ = glfwCreateWindow(config.Width, config.Height, config.Title.c_str(), monitor, nullptr);
	if (window_ == nullptr)
	{
		Throw(std::runtime_error("failed to create window"));
	}

	GLFWimage icon;
	icon.pixels = stbi_load("../assets/textures/Vulkan.png", &icon.width, &icon.height, nullptr, 4);
	if (icon.pixels == nullptr)
	{
		Throw(std::runtime_error("failed to load icon"));
	}

	glfwSetWindowIcon(window_, 1, &icon);
	stbi_image_free(icon.pixels);

	if (config.CursorDisabled)
	{
		glfwSetInputMode(window_, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
	}

	glfwSetWindowUserPointer(window_, this);
	glfwSetKeyCallback(window_, GlfwKeyCallback);
	glfwSetCursorPosCallback(window_, GlfwCursorPositionCallback);
	glfwSetMouseButtonCallback(window_, GlfwMouseButtonCallback);
	glfwSetScrollCallback(window_, GlfwScrollCallback);
	glfwSetDropCallback(window_, GlfwSetDropCallback);
#else
	window_ = static_cast<GLFWwindow*>(config.AndroidNativeWindow);
#endif
}

Window::~Window()
{
#if !ANDROID
	if (window_ != nullptr)
	{
		glfwDestroyWindow(window_);
		window_ = nullptr;
	}

	glfwTerminate();
	glfwSetErrorCallback(nullptr);
#endif
}

float Window::ContentScale() const
{
#if !ANDROID
	float xscale;
	float yscale;
	glfwGetWindowContentScale(window_, &xscale, &yscale);
#else
	float xscale = 1;
#endif
	return xscale;
}

VkExtent2D Window::FramebufferSize() const
{
#if !ANDROID
	int width, height;
	glfwGetFramebufferSize(window_, &width, &height);
	return VkExtent2D{ static_cast<uint32_t>(width), static_cast<uint32_t>(height) };
#else
	float aspect = ANativeWindow_getWidth(window_) / static_cast<float>(ANativeWindow_getHeight(window_));
	return VkExtent2D{ static_cast<uint32_t>(floorf(1920 * aspect)), 1920 };
#endif
}

VkExtent2D Window::WindowSize() const
{
#if !ANDROID
	int width, height;
	glfwGetWindowSize(window_, &width, &height);
	return VkExtent2D{ static_cast<uint32_t>(width), static_cast<uint32_t>(height) };
#else
    return VkExtent2D{ (uint32_t)ANativeWindow_getWidth(window_), (uint32_t)ANativeWindow_getHeight(window_) };
#endif
}

const char* Window::GetKeyName(const int key, const int scancode) const
{
#if !ANDROID
	return glfwGetKeyName(key, scancode);
#else
	return "A";
#endif
}

std::vector<const char*> Window::GetRequiredInstanceExtensions() const
{
#if !ANDROID
	uint32_t glfwExtensionCount = 0;
	const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
	return std::vector<const char*>(glfwExtensions, glfwExtensions + glfwExtensionCount);
#else
	return std::vector<const char*>();
#endif
}

double Window::GetTime() const
{
#if !ANDROID
	return glfwGetTime();
#else
	return now_ms() / 1000.0;
#endif
}

void Window::Close()
{
#if !ANDROID
	glfwSetWindowShouldClose(window_, 1);
#endif
}

bool Window::IsMinimized() const
{
	const auto size = FramebufferSize();
	return size.height == 0 && size.width == 0;
}

void Window::Run()
{
#if !ANDROID
	glfwSetTime(0.0);

	while (!glfwWindowShouldClose(window_))
	{
		glfwPollEvents();

		if (DrawFrame)
		{
			DrawFrame();
		}
	}
#endif
}

void Window::WaitForEvents() const
{
#if !ANDROID
	glfwWaitEvents();
#endif
}

void Window::Show() const
{
#if !ANDROID
	glfwShowWindow(window_);
#endif
}

constexpr double CLOSE_AREA_WIDTH = 0;
constexpr double TITLE_AREA_HEIGHT = 55;	
void Window::attemptDragWindow() {
#if !ANDROID
	if (glfwGetMouseButton(window_, 0) == GLFW_PRESS && dragState == 0) {
		glfwGetCursorPos(window_, &s_xpos, &s_ypos);
		glfwGetWindowSize(window_, &w_xsiz, &w_ysiz);
		dragState = 1;
	}
	if (glfwGetMouseButton(window_, 0) == GLFW_PRESS && dragState == 1) {
		double c_xpos, c_ypos;
		int w_xpos, w_ypos;
		glfwGetCursorPos(window_, &c_xpos, &c_ypos);
		glfwGetWindowPos(window_, &w_xpos, &w_ypos);
		if (
			s_xpos >= 0 && s_xpos <= ((double)w_xsiz - CLOSE_AREA_WIDTH) &&
			s_ypos >= 0 && s_ypos <= TITLE_AREA_HEIGHT) {
			glfwSetWindowPos(window_, w_xpos + (c_xpos - s_xpos), w_ypos + (c_ypos - s_ypos));
			}
		if (
			s_xpos >= ((double)w_xsiz - 15) && s_xpos <= ((double)w_xsiz) &&
			s_ypos >= ((double)w_ysiz - 15) && s_ypos <= ((double)w_ysiz)) {
			glfwSetWindowSize(window_, w_xsiz + (c_xpos - s_xpos), w_ysiz + (c_ypos - s_ypos));
			}
	}
	if (glfwGetMouseButton(window_, 0) == GLFW_RELEASE && dragState == 1) {
		dragState = 0;
	}
#endif
}
}
