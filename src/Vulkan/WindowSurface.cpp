#include "WindowSurface.hpp"
#include "Instance.hpp"
#include "Utilities/Exception.hpp"
#include "Utilities/StbImage.hpp"
#include "Common/CoreMinimal.hpp"
#include "Options.hpp"
#include "Utilities/FileHelper.hpp"
#include <spdlog/spdlog.h>

namespace Vulkan
{

// ============================================================================
// Window Implementation
// ============================================================================

Window::Window(const WindowConfig& config) :
    config_(config)
{
    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_VULKAN;
#if WIN32
    flags |= SDL_WINDOW_BORDERLESS;
#endif
    window_ = SDL_CreateWindow(config.Title.c_str(), config.Width, config.Height, flags);
    if (!window_)
    {
        Throw(std::runtime_error("failed to init SDL Window."));
    }
}

Window::~Window()
{
    if (window_ != nullptr)
    {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }
}

float Window::ContentScale() const
{
    float xscale = 1;
    xscale = SDL_GetWindowDisplayScale(window_);
    return xscale;
}

VkExtent2D Window::FramebufferSize() const
{
    int width, height;
    SDL_GetWindowSize(window_, &width, &height);
    return VkExtent2D{ static_cast<uint32_t>(width), static_cast<uint32_t>(height) };
}

VkExtent2D Window::WindowSize() const
{
    int width, height;
    SDL_GetWindowSize(window_, &width, &height);
    return VkExtent2D{ static_cast<uint32_t>(width), static_cast<uint32_t>(height) };
}

std::vector<const char*> Window::GetRequiredInstanceExtensions() const
{
    uint32_t extensionCount = 0;
    auto extensionNames = SDL_Vulkan_GetInstanceExtensions(&extensionCount);
    return std::vector<const char*>(extensionNames, extensionNames + extensionCount);
}

double Window::GetTime() const
{
    return SDL_GetTicks() / 1000.0;
}

void Window::Close()
{
    SDL_Event e{};
    e.type = SDL_EventType::SDL_EVENT_WINDOW_CLOSE_REQUESTED;
    e.window.windowID = SDL_GetWindowID(window_);
    SDL_PushEvent(&e);
}

bool Window::IsMinimized() const
{
    return SDL_GetWindowFlags(window_) & SDL_WINDOW_MINIMIZED;
}

bool Window::IsMaximumed() const
{
    //return glfwGetWindowAttrib(window_, GLFW_MAXIMIZED);
    return SDL_GetWindowFlags(window_) & SDL_WINDOW_MAXIMIZED;
}

void Window::WaitForEvents() const
{
    //glfwWaitEvents();
    SDL_Event event;
    while (SDL_WaitEvent(&event))
    {

    }
}

void Window::Show() const
{
    SDL_ShowWindow(window_);
}

void Window::Minimize()
{
    SDL_MinimizeWindow(window_);
}

void Window::Maximum()
{
    SDL_MaximizeWindow(window_);
}

void Window::Restore()
{
    SDL_RestoreWindow(window_);
}

constexpr double closeAreaWidth = 0;
constexpr double titleAreaHeight = 55;
void Window::attemptDragWindow()
{
#if !ANDROID
    float x{};
    float y{};
    SDL_MouseButtonFlags flag = SDL_GetMouseState(&x, &y);

    if (flag == SDL_BUTTON_LEFT && dragState == 0)
    {
        SDL_GetWindowSize(window_, &w_xsiz, &w_ysiz);

        s_xpos = x;
        s_ypos = y;
        dragState = 1;
    }
    if (flag == SDL_BUTTON_LEFT && dragState == 1)
    {
        double cXpos, cYpos;
        int wXpos, wYpos;
        SDL_GetWindowPosition(window_, &wXpos, &wYpos);

        cXpos = x;
        cYpos = y;

        if (
            s_xpos >= 0 && s_xpos <= (static_cast<double>(w_xsiz) - closeAreaWidth) &&
            s_ypos >= 0 && s_ypos <= titleAreaHeight)
        {
            SDL_SetWindowPosition(window_, wXpos + static_cast<int>(cXpos - s_xpos), wYpos + static_cast<int>(cYpos - s_ypos));
            capturedMouse_ = true;
        }
        if (
            s_xpos >= (static_cast<double>(w_xsiz) - 15) && s_xpos <= (static_cast<double>(w_xsiz)) &&
            s_ypos >= (static_cast<double>(w_ysiz) - 15) && s_ypos <= (static_cast<double>(w_ysiz)))
        {
            SDL_SetWindowSize(window_, w_xsiz + static_cast<int>(cXpos - s_xpos), w_ysiz + static_cast<int>(cYpos - s_ypos));
            capturedMouse_ = true;
        }
    }
    if (flag != SDL_BUTTON_LEFT && dragState == 1)
    {
        dragState = 0;
        capturedMouse_ = false;
    }
#endif
}

void Window::InitGLFW()
{
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD))
    {
        Throw(std::runtime_error("failed to init SDL."));
    }
    if (!SDL_Vulkan_LoadLibrary(nullptr))
    {
        Throw(std::runtime_error("failed to init SDL Vulkan."));
    }
}

void Window::TerminateGLFW()
{
    SDL_Vulkan_UnloadLibrary();
    SDL_Quit();
}

// ============================================================================
// Surface Implementation
// ============================================================================

Surface::Surface(const class Instance& instance) :
    instance_(instance)
{
    SDL_Vulkan_CreateSurface(instance.Window().Handle(), instance.Handle(), nullptr, &surface_);
}

Surface::~Surface()
{
    if (surface_ != nullptr)
    {
        vkDestroySurfaceKHR(instance_.Handle(), surface_, nullptr);
        surface_ = nullptr;
    }
}

}
