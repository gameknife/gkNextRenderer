#pragma once

#include "Engine/Vulkan/DebugUtilities.hpp"
#include <functional>
#include <vector>
#include <cstdint>
#include <string>

namespace Vulkan
{

// ============================================================================
// WindowConfig
// ============================================================================

struct WindowConfig final
{
    std::string Title;
    uint32_t Width;
    uint32_t Height;
    bool CursorDisabled;
    bool Fullscreen;
    bool Resizable;
    bool NeedScreenShot;
    void* AndroidNativeWindow;
    bool ForceSDR;
    bool HideTitleBar {};
    // Create the window with SDL_WINDOW_HIDDEN so it never pops to foreground or steals focus.
    // Used by agent validation captures and by the unit-test engine fixture. The swapchain still
    // renders and can be captured; relax to SDL_WINDOW_NOT_FOCUSABLE if a driver refuses to
    // present a hidden swapchain.
    bool HiddenWindow {};
};

// ============================================================================
// Window
// ============================================================================

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
    bool SetSize(uint32_t width, uint32_t height) const;

    void Minimize();
    void Maximum();
    void Restore();
    bool IsBorderlessFullscreen() const;
    bool SetBorderlessFullscreen(bool enable);
    bool ToggleBorderlessFullscreen();

    void ConfigureCustomTitleBarDrag(bool enabled, int titleBarHeight, int leftReservedWidth, int rightReservedWidth);

    // Static methods
    static void InitGLFW();
    static void TerminateGLFW();

private:
    static SDL_HitTestResult SDLCALL TitleBarHitTestCallback(SDL_Window* win, const SDL_Point* area, void* data);

    struct FCustomTitleBarDragState
    {
        bool enabled = false;
        bool hitTestSupported = false;
        int titleBarHeight = 0;
        int leftReservedWidth = 0;
        int rightReservedWidth = 0;
        int resizeBorder = 6;
    };

    const WindowConfig config_;
    Next_Window* window_{};

    FCustomTitleBarDragState customTitleBarDrag_;
};

// ============================================================================
// Surface
// ============================================================================

class Instance;

class Surface final
{
public:

    VULKAN_NON_COPIABLE(Surface)

    explicit Surface(const Instance& instance);
    ~Surface();

    const class Instance& Instance() const { return instance_; }

private:

    const class Instance& instance_;

    VULKAN_HANDLE(VkSurfaceKHR, surface_)
};

}
