// QuickJS Input/Audio/UI bindings: key/gamepad mapping, input event objects,
// audio playback and immediate-mode UI drawing.
// Split from QuickJSBindings.cpp; same namespace, separate TU.
#include "Engine/Runtime/Subsystems/QuickJSEngine.hpp"
#include "Engine/Runtime/Subsystems/QuickJSBindings.Internal.hpp"

#include "Engine/Runtime/Engine.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Assets/Core/Node.h"
#include "Engine/Runtime/Components/RenderComponent.h"
#include "Engine/Runtime/Components/PhysicsComponent.h"
#include "Engine/Runtime/Components/SkinnedMeshComponent.h"
#include "Engine/Runtime/Scene/SceneBuilder.h"
#include "Engine/Runtime/Reflection/PropertyAccessor.h"
#include "Engine/Runtime/Reflection/QuickJSReflectionBridge.h"
#include "Engine/Runtime/Reflection/QuickJSTypeConverter.h"
#include "Engine/Runtime/Subsystems/NextAudio.h"
#include "Engine/Runtime/Utilities/JsonHelpers.h"
#include "Engine/Assets/Loaders/FProcModel.h"
#include "Engine/Utilities/FileHelper.hpp"
#include "Engine/Runtime/Platform/PlatformCommon.h"

#include <imgui.h>
#include <nlohmann/json.hpp>
#include <spdlog/stopwatch.h>
#include <fstream>
#include <entt/core/hashed_string.hpp>
#include <cstdlib>
#include <limits>

#if WITH_QUICKJS
#include <ThirdParty/quickjs-ng/quickjspp.hpp>
#endif

#if WITH_QUICKJS
namespace NextQuickJSBindings
{
    std::optional<SDL_Keycode> KeyNameToCode(std::string name)
    {
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c)
        {
            return static_cast<char>(std::tolower(c));
        });

        if (name == "space") return SDLK_SPACE;
        if (name == "esc" || name == "escape") return SDLK_ESCAPE;
        if (name == "a") return SDLK_A;
        if (name == "w") return SDLK_W;
        if (name == "s") return SDLK_S;
        if (name == "d") return SDLK_D;
        if (name == "return" || name == "enter") return SDLK_RETURN;
        return std::nullopt;
    }

    std::optional<uint8_t> GamepadNameToButton(std::string name)
    {
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c)
        {
            return static_cast<char>(std::tolower(c));
        });

        if (name == "a" || name == "south") return SDL_GAMEPAD_BUTTON_SOUTH;
        if (name == "b" || name == "east") return SDL_GAMEPAD_BUTTON_EAST;
        if (name == "x" || name == "west") return SDL_GAMEPAD_BUTTON_WEST;
        if (name == "y" || name == "north") return SDL_GAMEPAD_BUTTON_NORTH;
        if (name == "start") return SDL_GAMEPAD_BUTTON_START;
        if (name == "back") return SDL_GAMEPAD_BUTTON_BACK;
        return std::nullopt;
    }

    std::string KeyCodeToName(const SDL_Keycode key)
    {
        switch (key)
        {
        case SDLK_SPACE:
            return "space";
        case SDLK_ESCAPE:
            return "esc";
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            return "enter";
        default:
            break;
        }

        std::string name = SDL_GetKeyName(key);
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c)
        {
            return static_cast<char>(std::tolower(c));
        });
        return name;
    }

    std::string GamepadButtonToName(const uint8_t button)
    {
        switch (button)
        {
        case SDL_GAMEPAD_BUTTON_SOUTH:
            return "south";
        case SDL_GAMEPAD_BUTTON_EAST:
            return "east";
        case SDL_GAMEPAD_BUTTON_WEST:
            return "west";
        case SDL_GAMEPAD_BUTTON_NORTH:
            return "north";
        case SDL_GAMEPAD_BUTTON_START:
            return "start";
        case SDL_GAMEPAD_BUTTON_BACK:
            return "back";
        default:
            return fmt::format("button{}", button);
        }
    }

    JSValue BuildInputEventObject(JSContext* ctx, const SDL_Event& event)
    {
        JSValue eventObject = JS_NewObject(ctx);
        switch (event.type)
        {
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
            JS_SetPropertyStr(ctx, eventObject, "type",
                              JS_NewString(ctx, event.type == SDL_EVENT_KEY_DOWN ? "keyDown" : "keyUp"));
            JS_SetPropertyStr(ctx, eventObject, "key",
                              JS_NewString(ctx, KeyCodeToName(event.key.key).c_str()));
            JS_SetPropertyStr(ctx, eventObject, "repeated", JS_NewBool(ctx, event.key.repeat));
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            JS_SetPropertyStr(ctx, eventObject, "type",
                              JS_NewString(ctx, event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? "mouseButtonDown" : "mouseButtonUp"));
            JS_SetPropertyStr(ctx, eventObject, "mouseButton", JS_NewUint32(ctx, event.button.button));
            break;
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        case SDL_EVENT_GAMEPAD_BUTTON_UP:
            JS_SetPropertyStr(ctx, eventObject, "type",
                              JS_NewString(ctx, event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ? "gamepadButtonDown" : "gamepadButtonUp"));
            JS_SetPropertyStr(ctx, eventObject, "gamepadButton",
                              JS_NewString(ctx, GamepadButtonToName(event.gbutton.button).c_str()));
            break;
        default:
            JS_SetPropertyStr(ctx, eventObject, "type", JS_NewString(ctx, "unknown"));
            break;
        }
        return eventObject;
    }

    JSValue InputIsKeyDown(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
    {
        (void)thisVal;
        if (argc < 1)
        {
            return JS_NewBool(ctx, false);
        }
        const std::string name = ToCString(ctx, argv[0]);
        if (name == "any")
        {
            return JS_NewBool(ctx, !GQuickJSInput.keysDown.empty());
        }
        const auto key = KeyNameToCode(name);
        return JS_NewBool(ctx, key.has_value() && GQuickJSInput.keysDown.contains(*key));
    }

    JSValue InputIsKeyPressed(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
    {
        (void)thisVal;
        if (argc < 1)
        {
            return JS_NewBool(ctx, false);
        }
        const std::string name = ToCString(ctx, argv[0]);
        if (name == "any")
        {
            return JS_NewBool(ctx,
                              !GQuickJSInput.keysPressed.empty() ||
                                  !GQuickJSInput.mouseButtonsPressed.empty() ||
                                  !GQuickJSInput.gamepadButtonsPressed.empty());
        }
        const auto key = KeyNameToCode(name);
        return JS_NewBool(ctx, key.has_value() && GQuickJSInput.keysPressed.contains(*key));
    }

    JSValue InputIsMouseButtonDown(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
    {
        (void)thisVal;
        if (argc < 1)
        {
            return JS_NewBool(ctx, false);
        }
        uint32_t button = 0;
        JS_ToUint32(ctx, &button, argv[0]);
        return JS_NewBool(ctx, GQuickJSInput.mouseButtonsDown.contains(static_cast<uint8_t>(button)));
    }

    JSValue InputIsMouseButtonPressed(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
    {
        (void)thisVal;
        if (argc < 1)
        {
            return JS_NewBool(ctx, false);
        }
        uint32_t button = 0;
        JS_ToUint32(ctx, &button, argv[0]);
        return JS_NewBool(ctx, GQuickJSInput.mouseButtonsPressed.contains(static_cast<uint8_t>(button)));
    }

    JSValue InputGetGamepadButton(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
    {
        (void)thisVal;
        if (argc < 1)
        {
            return JS_NewBool(ctx, false);
        }
        const auto button = GamepadNameToButton(ToCString(ctx, argv[0]));
        return JS_NewBool(ctx, button.has_value() && GQuickJSInput.gamepadButtonsDown.contains(*button));
    }

    JSValue AudioPlaySfx(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
    {
        (void)thisVal;
        if (argc < 1)
        {
            return JS_UNDEFINED;
        }
        const std::string path = ToCString(ctx, argv[0]);
        double volume = 1.0;
        if (argc >= 2)
        {
            JS_ToFloat64(ctx, &volume, argv[1]);
        }
        if (auto* engine = NextEngine::GetInstance(); engine && engine->GetAudio())
        {
            engine->GetAudio()->PlaySfx(path, static_cast<float>(volume));
        }
        return JS_UNDEFINED;
    }

    JSValue AudioPlayMusic(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
    {
        (void)thisVal;
        if (argc < 1)
        {
            return JS_UNDEFINED;
        }
        const std::string path = ToCString(ctx, argv[0]);
        double volume = 1.0;
        if (argc >= 2)
        {
            JS_ToFloat64(ctx, &volume, argv[1]);
        }
        if (auto* engine = NextEngine::GetInstance(); engine && engine->GetAudio())
        {
            engine->GetAudio()->PlayMusic(path, static_cast<float>(volume));
        }
        return JS_UNDEFINED;
    }

    JSValue AudioStopMusic(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
    {
        (void)ctx;
        (void)thisVal;
        (void)argc;
        (void)argv;
        if (auto* engine = NextEngine::GetInstance(); engine && engine->GetAudio())
        {
            engine->GetAudio()->StopMusic();
        }
        return JS_UNDEFINED;
    }

    JSValue UIBegin(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
    {
        (void)thisVal;
        if (argc < 1)
        {
            return JS_NewBool(ctx, false);
        }
        const std::string name = ToCString(ctx, argv[0]);
        int flags = 0;
        if (argc >= 2)
        {
            JS_ToInt32(ctx, &flags, argv[1]);
        }
        return JS_NewBool(ctx, ImGui::Begin(name.c_str(), nullptr, flags));
    }

    JSValue UIEnd(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
    {
        (void)ctx;
        (void)thisVal;
        (void)argc;
        (void)argv;
        ImGui::End();
        return JS_UNDEFINED;
    }

    JSValue UIText(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
    {
        (void)thisVal;
        if (argc >= 1)
        {
            const std::string text = ToCString(ctx, argv[0]);
            ImGui::TextUnformatted(text.c_str());
        }
        return JS_UNDEFINED;
    }

    JSValue UISetCursorPos(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
    {
        (void)thisVal;
        if (argc >= 2)
        {
            float x = 0.0f;
            float y = 0.0f;
            JSToFloat(ctx, argv[0], x);
            JSToFloat(ctx, argv[1], y);
            ImGui::SetCursorPos(ImVec2(x, y));
        }
        return JS_UNDEFINED;
    }

    JSValue UIGetWindowSize(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
    {
        (void)thisVal;
        (void)argc;
        (void)argv;
        const ImVec2 size = ImGui::GetWindowSize();
        return Vec2ToJS(ctx, glm::vec2(size.x, size.y));
    }

    JSValue UISetWindowFontScale(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
    {
        (void)thisVal;
        if (argc >= 1)
        {
            float scale = 1.0f;
            JSToFloat(ctx, argv[0], scale);
            ImGui::SetWindowFontScale(scale);
        }
        return JS_UNDEFINED;
    }

    JSValue UICalcTextSize(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
    {
        (void)thisVal;
        if (argc < 1)
        {
            return Vec2ToJS(ctx, glm::vec2(0.0f));
        }

        const std::string text = ToCString(ctx, argv[0]);
        float scale = 1.0f;
        if (argc >= 2)
        {
            JSToFloat(ctx, argv[1], scale);
        }

        const ImVec2 rawSize = ImGui::CalcTextSize(text.c_str());
        return Vec2ToJS(ctx, glm::vec2(rawSize.x * scale, rawSize.y * scale));
    }

    JSValue UIDrawText(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
    {
        (void)thisVal;
        if (argc < 3)
        {
            return JS_UNDEFINED;
        }

        const std::string text = ToCString(ctx, argv[0]);
        float x = 0.0f;
        float y = 0.0f;
        float scale = 1.0f;
        float r = 1.0f;
        float g = 1.0f;
        float b = 1.0f;
        float a = 1.0f;
        JSToFloat(ctx, argv[1], x);
        JSToFloat(ctx, argv[2], y);
        if (argc >= 4) JSToFloat(ctx, argv[3], scale);
        if (argc >= 5) JSToFloat(ctx, argv[4], r);
        if (argc >= 6) JSToFloat(ctx, argv[5], g);
        if (argc >= 7) JSToFloat(ctx, argv[6], b);
        if (argc >= 8) JSToFloat(ctx, argv[7], a);

        ImDrawList* drawList = ImGui::GetForegroundDrawList();
        if (!drawList)
        {
            return JS_UNDEFINED;
        }

        const auto toByte = [](float value) -> int
        {
            return static_cast<int>(glm::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
        };
        drawList->AddText(nullptr,
                          ImGui::GetFontSize() * std::max(scale, 0.01f),
                          ImVec2(x, y),
                          IM_COL32(toByte(r), toByte(g), toByte(b), toByte(a)),
                          text.c_str());
        return JS_UNDEFINED;
    }

    JSValue UIDrawRectFilled(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
    {
        (void)thisVal;
        if (argc < 8)
        {
            return JS_UNDEFINED;
        }

        float x = 0.0f;
        float y = 0.0f;
        float width = 0.0f;
        float height = 0.0f;
        float r = 1.0f;
        float g = 1.0f;
        float b = 1.0f;
        float a = 1.0f;
        float rounding = 0.0f;
        JSToFloat(ctx, argv[0], x);
        JSToFloat(ctx, argv[1], y);
        JSToFloat(ctx, argv[2], width);
        JSToFloat(ctx, argv[3], height);
        JSToFloat(ctx, argv[4], r);
        JSToFloat(ctx, argv[5], g);
        JSToFloat(ctx, argv[6], b);
        JSToFloat(ctx, argv[7], a);
        if (argc >= 9) JSToFloat(ctx, argv[8], rounding);

        ImDrawList* drawList = ImGui::GetForegroundDrawList();
        if (!drawList)
        {
            return JS_UNDEFINED;
        }

        const auto toByte = [](float value) -> int
        {
            return static_cast<int>(glm::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
        };
        drawList->AddRectFilled(ImVec2(x, y),
                                ImVec2(x + std::max(0.0f, width), y + std::max(0.0f, height)),
                                IM_COL32(toByte(r), toByte(g), toByte(b), toByte(a)),
                                std::max(0.0f, rounding));
        return JS_UNDEFINED;
    }

    JSValue UIDrawRect(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
    {
        (void)thisVal;
        if (argc < 8)
        {
            return JS_UNDEFINED;
        }

        float x = 0.0f;
        float y = 0.0f;
        float width = 0.0f;
        float height = 0.0f;
        float r = 1.0f;
        float g = 1.0f;
        float b = 1.0f;
        float a = 1.0f;
        float rounding = 0.0f;
        float thickness = 1.0f;
        JSToFloat(ctx, argv[0], x);
        JSToFloat(ctx, argv[1], y);
        JSToFloat(ctx, argv[2], width);
        JSToFloat(ctx, argv[3], height);
        JSToFloat(ctx, argv[4], r);
        JSToFloat(ctx, argv[5], g);
        JSToFloat(ctx, argv[6], b);
        JSToFloat(ctx, argv[7], a);
        if (argc >= 9) JSToFloat(ctx, argv[8], rounding);
        if (argc >= 10) JSToFloat(ctx, argv[9], thickness);

        ImDrawList* drawList = ImGui::GetForegroundDrawList();
        if (!drawList)
        {
            return JS_UNDEFINED;
        }

        const auto toByte = [](float value) -> int
        {
            return static_cast<int>(glm::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
        };
        drawList->AddRect(ImVec2(x, y),
                          ImVec2(x + std::max(0.0f, width), y + std::max(0.0f, height)),
                          IM_COL32(toByte(r), toByte(g), toByte(b), toByte(a)),
                          std::max(0.0f, rounding),
                          0,
                          std::max(0.5f, thickness));
        return JS_UNDEFINED;
    }
    
}
#endif
