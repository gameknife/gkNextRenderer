#include "WindowSurface.hpp"
#include "Instance.hpp"
#include "Utilities/Exception.hpp"
#include "Utilities/StbImage.hpp"
#include "Common/CoreMinimal.hpp"
#include "Options.hpp"
#include "Utilities/FileHelper.hpp"
#include <algorithm>
#include <spdlog/spdlog.h>

namespace Vulkan
{

// ============================================================================
// Window Implementation
// ============================================================================

SDL_HitTestResult SDLCALL Window::TitleBarHitTestCallback(SDL_Window* win, const SDL_Point* area, void* data)
{
    auto* self = static_cast<Window*>(data);
    if (!self || !area)
    {
        return SDL_HITTEST_NORMAL;
    }

    const auto& dragState = self->customTitleBarDrag_;
    if (!dragState.enabled)
    {
        return SDL_HITTEST_NORMAL;
    }

    int width = 0;
    int height = 0;
    SDL_GetWindowSize(win, &width, &height);
    if (width <= 0 || height <= 0)
    {
        return SDL_HITTEST_NORMAL;
    }

    const Uint64 windowFlags = SDL_GetWindowFlags(win);
    const bool isMaximized = (windowFlags & SDL_WINDOW_MAXIMIZED) != 0;
    if (dragState.resizeBorder > 0 && self->config_.Resizable && !isMaximized)
    {
        const int border = dragState.resizeBorder;
        const bool left = area->x >= 0 && area->x < border;
        const bool right = area->x >= width - border && area->x < width;
        const bool top = area->y >= 0 && area->y < border;
        const bool bottom = area->y >= height - border && area->y < height;

        if (top && left) return SDL_HITTEST_RESIZE_TOPLEFT;
        if (top && right) return SDL_HITTEST_RESIZE_TOPRIGHT;
        if (bottom && left) return SDL_HITTEST_RESIZE_BOTTOMLEFT;
        if (bottom && right) return SDL_HITTEST_RESIZE_BOTTOMRIGHT;
        if (top) return SDL_HITTEST_RESIZE_TOP;
        if (bottom) return SDL_HITTEST_RESIZE_BOTTOM;
        if (left) return SDL_HITTEST_RESIZE_LEFT;
        if (right) return SDL_HITTEST_RESIZE_RIGHT;
    }

    const int titleBarHeight = std::max(0, dragState.titleBarHeight);
    if (area->y < 0 || area->y >= titleBarHeight)
    {
        return SDL_HITTEST_NORMAL;
    }

    const int leftReserved = std::clamp(dragState.leftReservedWidth, 0, width);
    const int rightReserved = std::clamp(dragState.rightReservedWidth, 0, width);
    const int dragMinX = leftReserved;
    const int dragMaxX = std::max(dragMinX, width - rightReserved);

    if (area->x >= dragMinX && area->x < dragMaxX)
    {
        return SDL_HITTEST_DRAGGABLE;
    }

    return SDL_HITTEST_NORMAL;
}

Window::Window(const WindowConfig& config) :
    config_(config)
{
    SDL_WindowFlags flags = SDL_WINDOW_VULKAN;
    if (config.Resizable)
    {
        flags |= SDL_WINDOW_RESIZABLE;
    }
    if (config.HideTitleBar)
    {
        flags |= SDL_WINDOW_BORDERLESS;
    }
    window_ = SDL_CreateWindow(config.Title.c_str(), config.Width, config.Height, flags);
    if (!window_)
    {
        Throw(std::runtime_error("failed to init SDL Window."));
    }

    if (config.HideTitleBar)
    {
        if (!SDL_SetWindowBordered(window_, false))
        {
            SPDLOG_WARN("Failed to hide window title bar: {}", SDL_GetError());
        }

        customTitleBarDrag_.hitTestSupported = SDL_SetWindowHitTest(window_, TitleBarHitTestCallback, this);
        if (!customTitleBarDrag_.hitTestSupported)
        {
            SPDLOG_WARN("SDL_SetWindowHitTest is not available, custom title bar drag may not work: {}",
                SDL_GetError());
        }
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
    SDL_GetWindowSizeInPixels(window_, &width, &height);
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

void Window::ConfigureCustomTitleBarDrag(bool enabled, int titleBarHeight, int leftReservedWidth, int rightReservedWidth)
{
    if (!config_.HideTitleBar)
    {
        customTitleBarDrag_.enabled = false;
        return;
    }

    customTitleBarDrag_.enabled = enabled && customTitleBarDrag_.hitTestSupported;
    customTitleBarDrag_.titleBarHeight = std::max(0, titleBarHeight);
    customTitleBarDrag_.leftReservedWidth = std::max(0, leftReservedWidth);
    customTitleBarDrag_.rightReservedWidth = std::max(0, rightReservedWidth);
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
