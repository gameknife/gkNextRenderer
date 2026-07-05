#include "Modules/NextRemote/RemoteImGuiSession.hpp"

#include "Engine/Runtime/DebugUiProvider.hpp"
#include "Engine/Runtime/Editor/UserInterface.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/RemoteProtocol.hpp"
#include "Engine/Utilities/Exception.hpp"

#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_scancode.h>

#include <algorithm>

extern ImGuiKey ImGui_ImplSDL3_KeyEventToImGuiKey(SDL_Keycode keycode, SDL_Scancode scancode);

namespace Runtime::Remote
{
    namespace
    {
        struct FRemoteImGuiConfig
        {
            ImGuiConfigFlags configFlags = ImGuiConfigFlags_NavEnableKeyboard;
            bool moveWindowsFromTitleBarOnly = false;
        };

        FRemoteImGuiConfig CaptureRemoteImGuiConfig()
        {
            FRemoteImGuiConfig config;
            if (ImGui::GetCurrentContext() == nullptr)
            {
                return config;
            }

            const ImGuiIO& sourceIo = ImGui::GetIO();
            constexpr ImGuiConfigFlags supportedRemoteFlags =
                ImGuiConfigFlags_NavEnableKeyboard |
                ImGuiConfigFlags_NavEnableGamepad |
                ImGuiConfigFlags_DockingEnable |
                ImGuiConfigFlags_IsSRGB |
                ImGuiConfigFlags_IsTouchScreen;
            config.configFlags = sourceIo.ConfigFlags & supportedRemoteFlags;
            config.configFlags |= ImGuiConfigFlags_NavEnableKeyboard;
            config.moveWindowsFromTitleBarOnly = sourceIo.ConfigWindowsMoveFromTitleBarOnly;
            return config;
        }

        void AddModifierEvents(ImGuiIO& io, const SDL_Keymod mod)
        {
            io.AddKeyEvent(ImGuiMod_Ctrl, (mod & SDL_KMOD_CTRL) != 0);
            io.AddKeyEvent(ImGuiMod_Shift, (mod & SDL_KMOD_SHIFT) != 0);
            io.AddKeyEvent(ImGuiMod_Alt, (mod & SDL_KMOD_ALT) != 0);
            io.AddKeyEvent(ImGuiMod_Super, (mod & SDL_KMOD_GUI) != 0);
        }

        ImGuiMouseButton MouseButtonIndex(const uint8_t button)
        {
            switch (button)
            {
            case 2:
                return ImGuiMouseButton_Middle;
            case 3:
                return ImGuiMouseButton_Right;
            case 1:
            default:
                return ImGuiMouseButton_Left;
            }
        }

        float ScaleX(const float x, const VkExtent2D extent)
        {
            return std::clamp(x, 0.0f, 1.0f) * static_cast<float>(std::max(1u, extent.width));
        }

        float ScaleY(const float y, const VkExtent2D extent)
        {
            return std::clamp(y, 0.0f, 1.0f) * static_cast<float>(std::max(1u, extent.height));
        }
    }

    FRemoteImGuiSession::FContextScope::FContextScope(ImGuiContext* context)
    {
        previousContext_ = ImGui::GetCurrentContext();
        ImGui::SetCurrentContext(context);
    }

    FRemoteImGuiSession::FContextScope::~FContextScope()
    {
        ImGui::SetCurrentContext(previousContext_);
    }

    FRemoteImGuiSession::FRemoteImGuiSession(NextEngine& engine, std::string sessionId)
        : engine_(engine)
        , sessionId_(std::move(sessionId))
    {
        NextUI::UserInterface* userInterface = engine_.GetUserInterface();
        if (userInterface == nullptr)
        {
            Throw(std::runtime_error("remote imgui session requires an initialized UserInterface"));
        }

        const FRemoteImGuiConfig config = CaptureRemoteImGuiConfig();
        context_ = ImGui::CreateContext(userInterface->GetFontAtlas());
        ConfigureContext(config.configFlags, config.moveWindowsFromTitleBarOnly);
    }

    FRemoteImGuiSession::~FRemoteImGuiSession()
    {
        if (context_ == nullptr)
        {
            return;
        }

        FContextScope scope(context_);
        if (NextUI::UserInterface* userInterface = engine_.GetUserInterface())
        {
            auto& io = ImGui::GetIO();
            io.BackendRendererName = nullptr;
            io.BackendRendererUserData = nullptr;
            io.BackendFlags &= ~ImGuiBackendFlags_RendererHasVtxOffset;
            (void)userInterface;
        }
        ImGuiContext* destroyContext = context_;
        context_ = nullptr;
        ImGui::SetCurrentContext(nullptr);
        ImGui::DestroyContext(destroyContext);
    }

    void FRemoteImGuiSession::ConfigureContext(const ImGuiConfigFlags configFlags,
                                               const bool moveWindowsFromTitleBarOnly)
    {
        FContextScope scope(context_);
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;
        io.ConfigFlags |= configFlags;
        io.ConfigFlags &= ~ImGuiConfigFlags_ViewportsEnable;
        io.ConfigWindowsMoveFromTitleBarOnly = moveWindowsFromTitleBarOnly;
        io.BackendPlatformName = "gk_remote_headless";
        if (NextUI::UserInterface* userInterface = engine_.GetUserInterface())
        {
            userInterface->AttachRendererBackendToCurrentContext();
        }
        if (Runtime::IDebugUiProvider* styleProvider = engine_.GetDebugUiProvider())
        {
            styleProvider->ApplyUiStyle();
        }
    }

    void FRemoteImGuiSession::HandleInputEvents(const std::vector<FCloudInputEvent>& events, const VkExtent2D extent)
    {
        if (context_ == nullptr)
        {
            return;
        }

        FContextScope scope(context_);
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(static_cast<float>(std::max(1u, extent.width)),
                                static_cast<float>(std::max(1u, extent.height)));
        io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);

        for (const FCloudInputEvent& event : events)
        {
            switch (event.type)
            {
            case FCloudInputEvent::EType::Key:
                AddKeyEvent(event);
                break;
            case FCloudInputEvent::EType::MouseMove:
                AddMouseMoveEvent(event, extent);
                break;
            case FCloudInputEvent::EType::MouseButton:
                AddMouseButtonEvent(event, extent);
                break;
            case FCloudInputEvent::EType::Wheel:
                AddWheelEvent(event);
                break;
            case FCloudInputEvent::EType::TextUtf8:
                AddTextEvent(event);
                break;
            case FCloudInputEvent::EType::Gamepad:
                break;
            }
        }
    }

    ImDrawData* FRemoteImGuiSession::BuildDrawData(const VkExtent2D extent, const Assets::Camera& camera)
    {
        if (context_ == nullptr)
        {
            return nullptr;
        }

        FContextScope scope(context_);
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(static_cast<float>(std::max(1u, extent.width)),
                                static_cast<float>(std::max(1u, extent.height)));
        io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
        io.DeltaTime = std::max(1.0f / 240.0f, static_cast<float>(engine_.GetDeltaSeconds()));

        if (NextUI::UserInterface* userInterface = engine_.GetUserInterface())
        {
            userInterface->AttachRendererBackendToCurrentContext();
        }

        ImGui::NewFrame();
        if (NextGameInstanceBase* gameInstance = engine_.GetGameInstance())
        {
            NextGameInstanceBase::FGameUiFrameContext context;
            context.surfaceKind = NextGameInstanceBase::FGameUiFrameContext::ESurfaceKind::RemoteView;
            context.sessionId = sessionId_;
            context.framebufferExtent = extent;
            context.viewCamera = &camera;
            context.allowWindowCommands = false;
            gameInstance->OnRenderUI(context);
        }
        ImGui::Render();
        wantsCaptureKeyboard_ = io.WantCaptureKeyboard;
        wantsCaptureMouse_ = io.WantCaptureMouse;
        return ImGui::GetDrawData();
    }

    void FRemoteImGuiSession::AddKeyEvent(const FCloudInputEvent& event)
    {
        ImGuiIO& io = ImGui::GetIO();
        const SDL_Keymod mod = static_cast<SDL_Keymod>(event.mod);
        const SDL_Scancode scancode = static_cast<SDL_Scancode>(event.scancode);
        const SDL_Keycode keycode = SDL_GetKeyFromScancode(scancode, mod, true);
        AddModifierEvents(io, mod);
        const ImGuiKey imguiKey = ImGui_ImplSDL3_KeyEventToImGuiKey(keycode, scancode);
        if (imguiKey != ImGuiKey_None)
        {
            io.AddKeyEvent(imguiKey, event.down);
            io.SetKeyEventNativeData(imguiKey, static_cast<int>(keycode), static_cast<int>(scancode));
        }
    }

    void FRemoteImGuiSession::AddMouseMoveEvent(const FCloudInputEvent& event, const VkExtent2D extent)
    {
        if (event.mode != static_cast<uint8_t>(ERemoteMouseMoveMode::Absolute))
        {
            return;
        }
        ImGui::GetIO().AddMousePosEvent(ScaleX(event.x, extent), ScaleY(event.y, extent));
    }

    void FRemoteImGuiSession::AddMouseButtonEvent(const FCloudInputEvent& event, const VkExtent2D extent)
    {
        ImGuiIO& io = ImGui::GetIO();
        io.AddMousePosEvent(ScaleX(event.x, extent), ScaleY(event.y, extent));
        io.AddMouseButtonEvent(MouseButtonIndex(event.button), event.down);
    }

    void FRemoteImGuiSession::AddWheelEvent(const FCloudInputEvent& event)
    {
        ImGui::GetIO().AddMouseWheelEvent(event.x, event.y);
    }

    void FRemoteImGuiSession::AddTextEvent(const FCloudInputEvent& event)
    {
        if (!event.text.empty())
        {
            ImGui::GetIO().AddInputCharactersUTF8(event.text.c_str());
        }
    }
}
