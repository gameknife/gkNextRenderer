// NextEngine input handling: keyboard/mouse/touch/gamepad dispatch and
// debug shortcuts. Split from Engine.cpp; same class, separate TU.
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Assets/Core/Model.hpp"
#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Assets/GPU/Texture.hpp"
#include "Engine/Options.hpp"
#include "Engine/Rendering/VulkanBaseRenderer.hpp"
#include "Engine/Runtime/GameInstance.hpp"
#include "Engine/Runtime/RemoteProtocol.hpp"
#include "Engine/Runtime/Interface/DebugUiProvider.hpp"
#include "Engine/Runtime/Interface/UiOverlay.hpp"
#include "Engine/Runtime/Config/CVarSystem.hpp"
#include "Engine/Runtime/Config/UserSettings.hpp"
#include "Engine/Runtime/Editor/UserInterface.hpp"
#include "Engine/Runtime/Scene/SceneList.hpp"
#include "Engine/Runtime/Subsystems/TaskCoordinator.hpp"
#include "Engine/Runtime/Platform/PlatformCommon.hpp"
#include "Engine/Vulkan/SwapChain.hpp"
#include "Engine/Runtime/Profiling/FrameProfiler.hpp"
#include "Engine/Utilities/Localization.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <fmt/format.h>
#include <spdlog/stopwatch.h>

void NextEngine::OnKey(SDL_Event& event)
{
    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat)
    {
        const SDL_Keymod modifiers = SDL_GetModState();
#if __APPLE__
        const bool hasCommand = (modifiers & SDL_KMOD_GUI) != 0;
        if (hasCommand && event.key.key == SDLK_Q)
        {
            RequestClose();
            return;
        }
#endif

        const bool altPressed = (modifiers & SDL_KMOD_ALT) != 0;
        const bool isAltEnter =
            altPressed && (event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER);
        const bool hasShortcutModifier =
            (modifiers & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_SHIFT | SDL_KMOD_GUI)) != 0;
        const bool isF11 = event.key.key == SDLK_F11 && !hasShortcutModifier;

        if (isAltEnter || isF11)
        {
            if (services_.cvarSystem)
            {
                auto result = services_.cvarSystem->ExecuteCommand("cvar.toggle sys.borderlessFullscreen");
                if (!result.success)
                {
                    ToggleBorderlessFullscreen();
                }
            }
            else
            {
                ToggleBorderlessFullscreen();
            }
            return;
        }
    }

    if (userInterface_->WantsToCaptureKeyboard() || (uiOverlay_ && uiOverlay_->WantsToCaptureKeyboard()))
    {
        return;
    }

    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat)
    {
        if (debugUiProvider_ && debugUiProvider_->HandleRendererShortcut(event.key.key, true,
                                                                         config_.showFlags.DebugGraphicsPanel, *this))
        {
            return;
        }

        if (HandleDebugShortcut(event.key.key))
        {
            return;
        }
    }

    if (event.type == SDL_EVENT_KEY_DOWN)
    {
        const SDL_Keymod modifiers = SDL_GetModState();
        const bool hasCtrlOrCmd = (modifiers & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0;
        if (hasCtrlOrCmd)
        {
            const bool hasShift = (modifiers & SDL_KMOD_SHIFT) != 0;
            if (event.key.key == SDLK_Z)
            {
                if (hasShift ? commandHistory_.Redo() : commandHistory_.Undo())
                {
                    return;
                }
            }
            else if (event.key.key == SDLK_Y)
            {
                if (commandHistory_.Redo())
                {
                    return;
                }
            }
        }
    }

    if (gameInstance_->OnKey(event))
    {
        return;
    }

    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat)
    {
        if (debugUiProvider_ && debugUiProvider_->HandleViewModeShortcut(
                event.key.key, true, config_.showFlags.DebugGraphicsPanel, config_.showFlags))
        {
            return;
        }
    }
}

bool NextEngine::HandleDebugShortcut(SDL_Keycode key)
{
    struct FDebugShortcutOps
    {
        std::function<bool()> IsActive;
        std::function<void(bool)> SetActive;
    };

    if (key < SDLK_F1 || key > SDLK_F10)
    {
        return false;
    }

    std::optional<FDebugShortcutOps> shortcutOps;
    switch (key)
    {
    case SDLK_F1:
        shortcutOps = FDebugShortcutOps{
            .IsActive = [this]() { return config_.showFlags.DebugPhysicsOverlay; },
            .SetActive = [this](bool active) { config_.showFlags.DebugPhysicsOverlay = active; },
        };
        break;
    case SDLK_F2:
        shortcutOps = FDebugShortcutOps{
            .IsActive = [this]() { return config_.showFlags.DebugGraphicsPanel; },
            .SetActive = [this](bool active) { config_.showFlags.DebugGraphicsPanel = active; },
        };
        break;
    case SDLK_F3:
        shortcutOps = FDebugShortcutOps{
            .IsActive = [this]() { return config_.showFlags.DebugProfileOverlay; },
            .SetActive = [this](bool active) { config_.showFlags.DebugProfileOverlay = active; },
        };
        break;
    case SDLK_F4:
        shortcutOps = FDebugShortcutOps{
            .IsActive = [this]() { return config_.showFlags.DebugCVarPanel; },
            .SetActive = [this](bool active) { config_.showFlags.DebugCVarPanel = active; },
        };
        break;
    default:
        if (gameInstance_ && gameInstance_->SupportsAppDebugShortcut(key))
        {
            shortcutOps = FDebugShortcutOps{
                .IsActive = [this, key]() { return gameInstance_->IsAppDebugShortcutActive(key); },
                .SetActive = [this, key](bool active) { (void)gameInstance_->SetAppDebugShortcutActive(key, active); },
            };
        }
        break;
    }

    if (!shortcutOps.has_value())
    {
        return false;
    }

    const bool isActive = shortcutOps->IsActive();
    const bool engineOwnsShortcut = key == SDLK_F1 || key == SDLK_F2 || key == SDLK_F3 || key == SDLK_F4;

    if (engineOwnsShortcut)
    {
        config_.showFlags.DebugPhysicsOverlay = false;
        config_.showFlags.DebugGraphicsPanel = false;
        config_.showFlags.DebugCVarPanel = false;
        config_.showFlags.DebugProfileOverlay = false;
        if (!isActive)
        {
            shortcutOps->SetActive(true);
        }
        return true;
    }

    shortcutOps->SetActive(!isActive);
    return true;
}

void NextEngine::OnTouch(bool down, double xpos, double ypos)
{
    // OnMouseButton(GLFW_MOUSE_BUTTON_RIGHT, down ? GLFW_PRESS : GLFW_RELEASE, 0);
}

void NextEngine::OnTouchMove(double xpos, double ypos) { OnCursorPosition(xpos, ypos); }

void NextEngine::InjectRelativeMouse(float dx, float dy)
{
    inputState_.mousePos += glm::dvec2(dx, dy);
    OnCursorPosition(inputState_.mousePos.x, inputState_.mousePos.y);
}

void NextEngine::OnCursorPosition(const double xpos, const double ypos)
{
    if (!renderer_->HasSwapChain() ||
        (uiOverlay_ && (uiOverlay_->WantsToCaptureKeyboard() || uiOverlay_->WantsToCaptureMouse())))
    {
        return;
    }

    const bool wantsMouseThroughUi = gameInstance_->WantsMouseInputWhenUiCaptures();
    if ((userInterface_->WantsToCaptureKeyboard() || userInterface_->WantsToCaptureMouse()) && !wantsMouseThroughUi)
    {
        return;
    }

    if (gameInstance_->OnCursorPosition(xpos, ypos))
    {
        return;
    }
}

void NextEngine::OnMouseButton(SDL_Event& event)
{
    if (!renderer_->HasSwapChain() || (uiOverlay_ && uiOverlay_->WantsToCaptureMouse()))
    {
        return;
    }

    if (userInterface_->WantsToCaptureMouse() && !gameInstance_->WantsMouseInputWhenUiCaptures())
    {
        return;
    }

    if (gameInstance_->OnMouseButton(event))
    {
        return;
    }
}

void NextEngine::OnScroll(const double xoffset, const double yoffset)
{
    if (!renderer_->HasSwapChain() || (uiOverlay_ && uiOverlay_->WantsToCaptureMouse()))
    {
        return;
    }

    if (userInterface_->WantsToCaptureMouse() && !gameInstance_->WantsMouseInputWhenUiCaptures())
    {
        return;
    }

    gameInstance_->OnScroll(xoffset, yoffset);
}

void NextEngine::OnDropFile(const char* dropPath)
{
    const std::string path(dropPath);
    const std::filesystem::path droppedPath(path);

    if (Runtime::Scene::SceneList::IsSupportedScenePath(droppedPath))
    {
        RequestLoadScene({.filename = path});
        return;
    }

    std::string ext = droppedPath.has_extension() ? droppedPath.extension().string() : std::string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (ext == ".hdr")
    {
        uint32_t newTextureId = Assets::GlobalTexturePool::GetInstance()->LoadHDRTexture(path);
        scene_->GetEnvSettings().SkyIdx = newTextureId;
    }
}

void NextEngine::TickGamepadInput()
{
    int gamepadCount = 0;
    SDL_JoystickID* gamepads = SDL_GetGamepads(&gamepadCount);

    if (gamepadCount > 0)
    {
        SDL_Gamepad* masterGamepad = SDL_GetGamepadFromID(*gamepads);

        gameInstance_->OnGamepadInput(SDL_GetGamepadAxis(masterGamepad, SDL_GAMEPAD_AXIS_LEFTX),
                                      SDL_GetGamepadAxis(masterGamepad, SDL_GAMEPAD_AXIS_LEFTY),
                                      SDL_GetGamepadAxis(masterGamepad, SDL_GAMEPAD_AXIS_RIGHTX),
                                      SDL_GetGamepadAxis(masterGamepad, SDL_GAMEPAD_AXIS_RIGHTY),
                                      SDL_GetGamepadAxis(masterGamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER),
                                      SDL_GetGamepadAxis(masterGamepad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER));
    }

    SDL_free(gamepads);
}
