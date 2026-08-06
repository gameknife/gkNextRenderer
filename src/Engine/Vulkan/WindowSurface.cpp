#include "Engine/Vulkan/WindowSurface.hpp"
#include "Engine/Vulkan/Instance.hpp"
#include "Engine/Vulkan/VulkanInterposer.hpp"
#include "Engine/Vulkan/VulkanLoaderBypass.hpp"
#include "Engine/Utilities/Exception.hpp"
#include "Engine/Utilities/StbImage.hpp"
#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Options.hpp"
#include "Engine/Utilities/FileHelper.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>

namespace Vulkan
{

namespace
{
#if WIN32
    std::filesystem::path FindIcdManifest(const std::string& vulkanDriver)
    {
        const bool isDozen = vulkanDriver == "dozen";
        const char* overrideVariable = isDozen ? "GK_NEXT_DOZEN_ICD" : "GK_NEXT_LVP_ICD";
        if (const char* overridePath = std::getenv(overrideVariable);
            overridePath != nullptr && overridePath[0] != '\0')
        {
            std::error_code errorCode;
            const std::filesystem::path path = std::filesystem::absolute(
                std::filesystem::path(overridePath), errorCode);
            if (!errorCode && std::filesystem::is_regular_file(path, errorCode))
            {
                return path;
            }
            return {};
        }

        const std::filesystem::path executableDirectory = NextRenderer::GetExecutableDirectory();
        const std::filesystem::path runtimeRoot = Utilities::FileHelper::GetRuntimeRoot();
#if defined(GK_NEXT_SOURCE_DIR)
        const std::filesystem::path sourceRoot = GK_NEXT_SOURCE_DIR;
#else
        const std::filesystem::path sourceRoot;
#endif
        const std::string manifestPrefix = isDozen ? "dzn_icd" : "lvp_icd";
        const std::array<std::filesystem::path, 16> candidates =
        {
            sourceRoot / "external" / "Dozen" / "x64" / (manifestPrefix + ".x86_64.json"),
            sourceRoot / "external" / "Dozen" / "x64" / (manifestPrefix + ".x64.json"),
            sourceRoot / "external" / "mesa" / "x64" / (manifestPrefix + ".x86_64.json"),
            sourceRoot / "external" / "mesa" / "x64" / (manifestPrefix + ".x64.json"),
            executableDirectory / (manifestPrefix + ".x86_64.json"),
            executableDirectory / (manifestPrefix + ".x64.json"),
            executableDirectory / (manifestPrefix + ".json"),
            executableDirectory / (isDozen ? "dozen" : "lvp") / (manifestPrefix + ".x86_64.json"),
            executableDirectory / "mesa" / (manifestPrefix + ".x86_64.json"),
            runtimeRoot / (manifestPrefix + ".x86_64.json"),
            runtimeRoot / (isDozen ? "dozen" : "lvp") / (manifestPrefix + ".x86_64.json"),
            runtimeRoot / "mesa" / (manifestPrefix + ".x86_64.json"),
            runtimeRoot / "Vulkan" / (manifestPrefix + ".x86_64.json"),
            sourceRoot / "external" / "Vulkan" / "x64" / (manifestPrefix + ".x86_64.json"),
            executableDirectory / "Vulkan" / (manifestPrefix + ".x86_64.json"),
            runtimeRoot / "Vulkan" / (manifestPrefix + ".x64.json"),
        };

        std::error_code errorCode;
        for (const std::filesystem::path& candidate : candidates)
        {
            if (!candidate.empty() && std::filesystem::is_regular_file(candidate, errorCode))
            {
                return std::filesystem::absolute(candidate, errorCode);
            }
            errorCode.clear();
        }

        return {};
    }

    bool SetVulkanEnvironmentVariable(const char* name, const char* value)
    {
        if (SetEnvironmentVariableA(name, value) != TRUE)
        {
            SPDLOG_ERROR("Failed to set Vulkan environment variable {}: {}", name, GetLastError());
            return false;
        }
        return true;
    }

    bool PrependPathEntry(const std::filesystem::path& directory)
    {
        if (directory.empty())
        {
            return false;
        }

        const std::string directoryString = directory.string();
        const char* existingPath = std::getenv("PATH");
        const std::string pathValue = existingPath != nullptr && existingPath[0] != '\0'
            ? directoryString + ";" + existingPath
            : directoryString;
        return SetVulkanEnvironmentVariable("PATH", pathValue.c_str());
    }

    void ConfigureVulkanDriver(const std::string& vulkanDriver)
    {
        if (vulkanDriver == "native")
        {
            return;
        }

        const std::filesystem::path manifest = FindIcdManifest(vulkanDriver);
        if (manifest.empty())
        {
            Throw(std::runtime_error(vulkanDriver == "dozen"
                ? "Dozen Vulkan ICD manifest was not found; expected dzn_icd.x86_64.json or set GK_NEXT_DOZEN_ICD"
                : "LVP Vulkan ICD manifest was not found; expected lvp_icd.x86_64.json or set GK_NEXT_LVP_ICD"));
        }

        const std::filesystem::path driverDirectory = manifest.parent_path();
        const std::string manifestPath = manifest.string();
        if (!SetVulkanEnvironmentVariable("VK_DRIVER_FILES", manifestPath.c_str()) ||
            !SetVulkanEnvironmentVariable("VK_ICD_FILENAMES", manifestPath.c_str()) ||
            !SetVulkanEnvironmentVariable("VK_LOADER_DRIVERS_SELECT", nullptr) ||
            !SetVulkanEnvironmentVariable("VK_LOADER_LAYERS_DISABLE", "~implicit~") ||
            !PrependPathEntry(driverDirectory))
        {
            Throw(std::runtime_error("failed to configure the LVP Vulkan ICD"));
        }

        SPDLOG_INFO("Vulkan driver mode: {}; using ICD manifest {}",
                    vulkanDriver == "dozen" ? "Dozen" : "LVP", manifestPath);
    }
#else
    void ConfigureVulkanDriver(const std::string& vulkanDriver)
    {
        if (vulkanDriver != "native")
        {
            Throw(std::runtime_error("--vulkan-driver lvp and dozen are only supported on Windows"));
        }
    }
#endif
}

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
    if ((windowFlags & SDL_WINDOW_FULLSCREEN) != 0)
    {
        return SDL_HITTEST_NORMAL;
    }

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
    if (config.HiddenWindow)
    {
        // Keep the window out of the way: hidden so it never pops to foreground and steals focus
        // (agent validation captures, unit-test engine fixture). If a driver cannot present to a
        // hidden swapchain this can be relaxed to SDL_WINDOW_NOT_FOCUSABLE (still presents, just
        // never activates).
        flags |= SDL_WINDOW_HIDDEN;
    }

    uint32_t windowWidth = config.Width;
    uint32_t windowHeight = config.Height;

    if (!config.Fullscreen)
    {
        const SDL_DisplayID displayId = SDL_GetPrimaryDisplay();
        float displayScale = 1.0f;
#if WIN32
        if (!config.SystemDpiScaling)
        {
            displayScale = SDL_GetDisplayContentScale(displayId);
            if (!std::isfinite(displayScale) || displayScale <= 0.0f)
            {
                displayScale = 1.0f;
            }

            // WindowConfig dimensions are logical UI dimensions. In per-monitor DPI-aware mode
            // Windows no longer bitmap-stretches the application for us, so allocate the matching
            // physical window size explicitly. Vulkan and ImGui can then render every pixel sharply.
            windowWidth = static_cast<uint32_t>(std::lround(static_cast<double>(windowWidth) * displayScale));
            windowHeight = static_cast<uint32_t>(std::lround(static_cast<double>(windowHeight) * displayScale));
        }
#endif

        SDL_Rect bounds;
        if (!SDL_GetDisplayUsableBounds(displayId, &bounds))
        {
            SDL_GetDisplayBounds(displayId, &bounds);
        }
        if (bounds.w > 0 && bounds.h > 0)
        {
            const uint32_t maxW = static_cast<uint32_t>(bounds.w);
            const uint32_t maxH = static_cast<uint32_t>(bounds.h);
            if (windowWidth > maxW || windowHeight > maxH)
            {
                const float aspect = static_cast<float>(windowWidth) / static_cast<float>(windowHeight);
                if (windowWidth > maxW)
                {
                    windowWidth = maxW;
                    windowHeight = static_cast<uint32_t>(static_cast<float>(maxW) / aspect);
                }
                if (windowHeight > maxH)
                {
                    windowHeight = maxH;
                    windowWidth = static_cast<uint32_t>(static_cast<float>(maxH) * aspect);
                }
            }
        }

#if WIN32
        if (config.SystemDpiScaling)
        {
            SPDLOG_INFO("Creating legacy system-DPI-scaled window at {}x{}", windowWidth, windowHeight);
        }
        else
        {
            SPDLOG_INFO("Creating DPI-aware window at {}x{} (logical {}x{}, scale {:.2f})",
                        windowWidth, windowHeight, config.Width, config.Height, displayScale);
        }
#endif
    }

    window_ = SDL_CreateWindow(config.Title.c_str(), windowWidth, windowHeight, flags);
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

    if (!SetBorderlessFullscreen(config.Fullscreen))
    {
        SPDLOG_WARN("Failed to apply startup fullscreen state: {}", config.Fullscreen);
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
#if WIN32
    if (config_.SystemDpiScaling)
    {
        return 1.0f;
    }
#endif
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

void Window::Show() const
{
    SDL_ShowWindow(window_);
}

bool Window::SetSize(uint32_t width, uint32_t height) const
{
    if (width == 0 || height == 0)
    {
        return false;
    }

    if (!SDL_SetWindowSize(window_, static_cast<int>(width), static_cast<int>(height)))
    {
        SPDLOG_WARN("Failed to resize SDL window to {}x{}: {}", width, height, SDL_GetError());
        return false;
    }

    if (!SDL_SyncWindow(window_))
    {
        SPDLOG_WARN("Window size synchronization timed out after resize: {}", SDL_GetError());
    }
    return true;
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

bool Window::IsBorderlessFullscreen() const
{
    return (SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN) != 0;
}

bool Window::SetBorderlessFullscreen(bool enable)
{
    if (IsBorderlessFullscreen() == enable)
    {
        return true;
    }

    if (!SDL_SetWindowFullscreen(window_, enable))
    {
        SPDLOG_WARN("Failed to set borderless fullscreen={} : {}", enable, SDL_GetError());
        return false;
    }

    if (!SDL_SyncWindow(window_))
    {
        SPDLOG_WARN("Fullscreen state synchronization timed out: {}", SDL_GetError());
    }

    return true;
}

bool Window::ToggleBorderlessFullscreen()
{
    return SetBorderlessFullscreen(!IsBorderlessFullscreen());
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

void Window::InitSDL(bool systemDpiScaling, const std::string& vulkanDriver)
{
#if WIN32
    const char* dpiAwareness = systemDpiScaling ? "unaware" : "permonitorv2";
    if (!SDL_SetHintWithPriority("SDL_WINDOWS_DPI_AWARENESS", dpiAwareness, SDL_HINT_OVERRIDE))
    {
        SPDLOG_WARN("Failed to set SDL Windows DPI awareness to {}: {}", dpiAwareness, SDL_GetError());
    }
#else
    (void)systemDpiScaling;
#endif
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD))
    {
        Throw(std::runtime_error("failed to init SDL."));
    }
    ConfigureVulkanDriver(vulkanDriver);

    // Software / translation ICDs cannot go through the Streamline interposer (see
    // VulkanLoaderBypass.hpp), so both the engine and SDL talk to the loader directly.
    if (vulkanDriver != "native")
    {
        Vulkan::RedirectVulkanImportsToSystemLoader();
        if (!SDL_Vulkan_LoadLibrary(nullptr))
        {
            Throw(std::runtime_error("failed to init SDL Vulkan."));
        }
        return;
    }

    const char* vulkanLoaderPath = Vulkan::Interposer().PreferredVulkanLoaderPath();
    if (!SDL_Vulkan_LoadLibrary(vulkanLoaderPath))
    {
        if (vulkanLoaderPath != nullptr)
        {
            SPDLOG_WARN("Failed to load Streamline Vulkan interposer through SDL: {}; falling back to default Vulkan loader",
                        SDL_GetError());
            if (SDL_Vulkan_LoadLibrary(nullptr))
            {
                return;
            }
        }
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
#if WIN32
    VkWin32SurfaceCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    createInfo.hinstance = GetModuleHandleW(nullptr);
    createInfo.hwnd = static_cast<HWND>(SDL_GetPointerProperty(
        SDL_GetWindowProperties(instance.Window().Handle()),
        SDL_PROP_WINDOW_WIN32_HWND_POINTER,
        nullptr));
    if (createInfo.hwnd == nullptr)
    {
        Throw(std::runtime_error("failed to obtain Win32 window handle from SDL."));
    }
    Check(Vulkan::Interposer().CreateWin32SurfaceKHR(
              instance.Handle(), &createInfo, nullptr, &surface_),
          "create Win32 window surface");
#else
    SDL_Vulkan_CreateSurface(instance.Window().Handle(), instance.Handle(), nullptr, &surface_);
#endif
}

Surface::~Surface()
{
    if (surface_ != nullptr)
    {
        Vulkan::Interposer().DestroySurfaceKHR(instance_.Handle(), surface_, nullptr);
        surface_ = nullptr;
    }
}

}
