#pragma once

#include "Vulkan.hpp"
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

    void Minimize();
    void Maximum();
    void Restore();

    void attemptDragWindow();

    // Static methods
    static void InitGLFW();
    static void TerminateGLFW();

    bool IsCapturingMouse() const { return capturedMouse_; }

private:

    const WindowConfig config_;
    Next_Window* window_{};

    bool capturedMouse_ = false;
    double s_xpos = 0, s_ypos = 0;
    int w_xsiz = 0, w_ysiz = 0;
    int dragState = 0;
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
