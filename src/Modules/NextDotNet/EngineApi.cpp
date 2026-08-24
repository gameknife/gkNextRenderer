#include "Modules/NextDotNet/EngineApi.hpp"

#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Assets/Loaders/FProcModel.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Components/PhysicsComponent.hpp"
#include "Engine/Runtime/Components/RenderComponent.hpp"
#include "Engine/Runtime/Interface/UserInterface.hpp"
#include "Engine/Runtime/Reflection/PropertyAccessor.hpp"
#include "Engine/Runtime/Scene/NodeUtils.hpp"
#include "Engine/Runtime/Scene/SceneBuilder.hpp"
#include "Engine/Runtime/Subsystems/NextAudio.hpp"
#include "Engine/Runtime/Subsystems/NextPhysics.hpp"
#include "Engine/Utilities/FileHelper.hpp"

#include <imgui.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <cstring>
#include <filesystem>
#include <string>

// Implementations behind EngineApi.def.h. Every function here is reachable from managed code, so
// each one has to survive being called with whatever a script passes: a missing engine, an empty
// string, an out-of-range id, a SceneBuild call outside its window. The QuickJS bindings threw JS
// exceptions for those; there is no equivalent across this ABI, so the rule is to log once and
// return a harmless value.

namespace Modules::NextDotNet
{
    FSceneBuildContext GSceneBuildContext;
    FInputState GInputState;

    namespace
    {
        std::string ToString(GkStr value)
        {
            return value.Data != nullptr && value.Length > 0
                       ? std::string(value.Data, static_cast<size_t>(value.Length))
                       : std::string();
        }

        glm::vec3 ToGlm(const FVec3* value)
        {
            return value != nullptr ? glm::vec3(value->X, value->Y, value->Z) : glm::vec3(0.0f);
        }

        glm::quat ToGlmQuat(const FVec4* value)
        {
            if (value == nullptr)
            {
                return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
            }
            const glm::quat rotation(value->W, value->X, value->Y, value->Z);
            const float lengthSquared = glm::dot(rotation, rotation);
            return lengthSquared > 0.000001f ? glm::normalize(rotation) : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        }

        void StoreVec2(FVec2* out, float x, float y)
        {
            if (out != nullptr)
            {
                out->X = x;
                out->Y = y;
            }
        }

        /// Shared by every string-returning binding: reports the length the caller needs and fills
        /// the buffer when there is room. A null buffer is the documented probe call.
        int32_t WriteString(const std::string& text, char* buffer, int32_t capacity)
        {
            const int32_t length = static_cast<int32_t>(text.size());
            if (buffer == nullptr || capacity <= 0)
            {
                return length;
            }
            const int32_t copied = std::min(length, capacity);
            std::memcpy(buffer, text.data(), static_cast<size_t>(copied));
            return copied;
        }

        /// SDL resolves every key name it knows; the aliases are the ones QuickJS scripts used and
        /// that SDL spells differently.
        SDL_Keycode ResolveKeyName(const std::string& name)
        {
            std::string normalized = name;
            std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

            if (normalized == "esc") return SDLK_ESCAPE;
            if (normalized == "enter") return SDLK_RETURN;
            if (normalized == "ctrl") return SDLK_LCTRL;
            if (normalized == "alt") return SDLK_LALT;
            if (normalized == "shift") return SDLK_LSHIFT;

            return SDL_GetKeyFromName(normalized.c_str());
        }

        uint8_t ResolveGamepadButtonName(const std::string& name)
        {
            std::string normalized = name;
            std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

            if (normalized == "a" || normalized == "south") return SDL_GAMEPAD_BUTTON_SOUTH;
            if (normalized == "b" || normalized == "east") return SDL_GAMEPAD_BUTTON_EAST;
            if (normalized == "x" || normalized == "west") return SDL_GAMEPAD_BUTTON_WEST;
            if (normalized == "y" || normalized == "north") return SDL_GAMEPAD_BUTTON_NORTH;
            if (normalized == "start") return SDL_GAMEPAD_BUTTON_START;
            if (normalized == "back") return SDL_GAMEPAD_BUTTON_BACK;
            return static_cast<uint8_t>(SDL_GAMEPAD_BUTTON_INVALID);
        }

        void StoreVec3(FVec3& out, const glm::vec3& value)
        {
            out = {value.x, value.y, value.z};
        }

        void StoreQuat(FVec4& out, const glm::quat& value)
        {
            out = {value.x, value.y, value.z, value.w};
        }

        bool IsFinite(const glm::vec3& value)
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        bool TryConvertMotionType(GkPhysicsMotionType value, NextMotionType& out)
        {
            switch (value)
            {
            case GkPhysicsMotionType::Static: out = NextMotionType::Static; return true;
            case GkPhysicsMotionType::Kinematic: out = NextMotionType::Kinematic; return true;
            case GkPhysicsMotionType::Dynamic: out = NextMotionType::Dynamic; return true;
            default: return false;
            }
        }

        GkPhysicsMotionType ConvertMotionType(NextMotionType value)
        {
            switch (value)
            {
            case NextMotionType::Kinematic: return GkPhysicsMotionType::Kinematic;
            case NextMotionType::Dynamic: return GkPhysicsMotionType::Dynamic;
            case NextMotionType::Static:
            default: return GkPhysicsMotionType::Static;
            }
        }

        bool TryConvertNodeMobility(GkNodeMobility value, Runtime::ENodeMobility& out)
        {
            switch (value)
            {
            case GkNodeMobility::Static: out = Runtime::ENodeMobility::Static; return true;
            case GkNodeMobility::Dynamic: out = Runtime::ENodeMobility::Dynamic; return true;
            case GkNodeMobility::Kinematic: out = Runtime::ENodeMobility::Kinematic; return true;
            default: return false;
            }
        }

        NextPhysics* GetPhysicsEngine()
        {
            NextEngine* engine = NextEngine::GetInstance();
            return engine ? engine->GetPhysicsEngine() : nullptr;
        }

        NextBodyID ToBodyId(uint32_t value)
        {
            return NextBodyID(value);
        }

        bool HasPhysicsBody(NextPhysics* physics, uint32_t bodyId)
        {
            const NextBodyID id = ToBodyId(bodyId);
            return physics != nullptr && !id.IsInvalid() && physics->GetBody(id) != nullptr;
        }

        ImGuiMouseButton ResolveImGuiMouseButton(int32_t button)
        {
            switch (button)
            {
            case SDL_BUTTON_LEFT: return ImGuiMouseButton_Left;
            case SDL_BUTTON_RIGHT: return ImGuiMouseButton_Right;
            case SDL_BUTTON_MIDDLE: return ImGuiMouseButton_Middle;
            case SDL_BUTTON_X1: return 3;
            case SDL_BUTTON_X2: return 4;
            default: return -1;
            }
        }

        bool RequireSceneBuild(const char* what)
        {
            if (GSceneBuildContext.IsValid())
            {
                return true;
            }
            SPDLOG_WARN("[dotnet] {} is only available inside the BeforeSceneRebuild hook", what);
            return false;
        }

        // --- logging -----------------------------------------------------------------------------

        void Log_Info(GkStr message) { SPDLOG_INFO("[script] {}", ToString(message)); }
        void Log_Warn(GkStr message) { SPDLOG_WARN("[script] {}", ToString(message)); }
        void Log_Error(GkStr message) { SPDLOG_ERROR("[script] {}", ToString(message)); }

        // --- engine ------------------------------------------------------------------------------

        uint32_t Engine_GetTotalFrames()
        {
            auto* engine = NextEngine::GetInstance();
            return engine != nullptr ? engine->GetTotalFrames() : 0;
        }

        double Engine_GetTime()
        {
            auto* engine = NextEngine::GetInstance();
            return engine != nullptr ? engine->GetTime() : 0.0;
        }

        double Engine_GetDeltaSeconds()
        {
            auto* engine = NextEngine::GetInstance();
            return engine != nullptr ? engine->GetDeltaSeconds() : 0.0;
        }

        double Engine_GetSmoothDeltaSeconds()
        {
            auto* engine = NextEngine::GetInstance();
            return engine != nullptr ? engine->GetSmoothDeltaSeconds() : 0.0;
        }

        void Engine_RequestLoadScene(GkStr filename)
        {
            if (auto* engine = NextEngine::GetInstance())
            {
                engine->RequestLoadScene({.filename = ToString(filename)});
            }
        }

        void Engine_RequestClose()
        {
            if (auto* engine = NextEngine::GetInstance())
            {
                engine->RequestClose();
            }
        }

        GkBool Engine_IsReplayMode() { return GOption != nullptr && GOption->FlappyReplay ? 1 : 0; }

        // --- input -------------------------------------------------------------------------------

        GkBool Input_IsKeyDown(GkStr name)
        {
            const std::string keyName = ToString(name);
            if (keyName.empty())
            {
                return GInputState.keysDown.empty() ? 0 : 1;
            }
            const SDL_Keycode key = ResolveKeyName(keyName);
            return key != SDLK_UNKNOWN && GInputState.keysDown.contains(key) ? 1 : 0;
        }

        GkBool Input_IsKeyPressed(GkStr name)
        {
            const std::string keyName = ToString(name);
            if (keyName.empty())
            {
                // "any input this frame", the form Flappy's replay depends on.
                return !GInputState.keysPressed.empty() || !GInputState.mouseButtonsPressed.empty() ||
                               !GInputState.gamepadButtonsPressed.empty()
                           ? 1
                           : 0;
            }
            const SDL_Keycode key = ResolveKeyName(keyName);
            return key != SDLK_UNKNOWN && GInputState.keysPressed.contains(key) ? 1 : 0;
        }

        GkBool Input_IsMouseButtonDown(int32_t button)
        {
            if (ImGui::GetCurrentContext() != nullptr)
            {
                const ImGuiMouseButton imguiButton = ResolveImGuiMouseButton(button);
                if (imguiButton >= 0 && ImGui::IsMouseDown(imguiButton))
                {
                    return 1;
                }
            }
            return GInputState.mouseButtonsDown.contains(static_cast<uint8_t>(button)) ? 1 : 0;
        }

        GkBool Input_IsMouseButtonPressed(int32_t button)
        {
            if (ImGui::GetCurrentContext() != nullptr)
            {
                const ImGuiMouseButton imguiButton = ResolveImGuiMouseButton(button);
                if (imguiButton >= 0 && ImGui::IsMouseClicked(imguiButton))
                {
                    return 1;
                }
            }
            return GInputState.mouseButtonsPressed.contains(static_cast<uint8_t>(button)) ? 1 : 0;
        }

        GkBool Input_IsGamepadButtonDown(GkStr name)
        {
            const uint8_t button = ResolveGamepadButtonName(ToString(name));
            return button != static_cast<uint8_t>(SDL_GAMEPAD_BUTTON_INVALID) &&
                           GInputState.gamepadButtonsDown.contains(button)
                       ? 1
                       : 0;
        }

        void Input_GetMousePosition(FVec2* outPosition)
        {
            if (ImGui::GetCurrentContext() != nullptr)
            {
                const ImVec2 position = ImGui::GetIO().MousePos;
                StoreVec2(outPosition, position.x, position.y);
                return;
            }
            if (auto* engine = NextEngine::GetInstance())
            {
                const glm::dvec2 position = engine->GetMousePos();
                StoreVec2(outPosition, static_cast<float>(position.x), static_cast<float>(position.y));
                return;
            }
            StoreVec2(outPosition, 0.0f, 0.0f);
        }

        float Input_GetGamepadAxis(int32_t axis)
        {
            if (axis < 0 || axis >= static_cast<int32_t>(GInputState.gamepadAxes.size()))
            {
                return 0.0f;
            }
            constexpr float inverseAxisMax = 1.0f / 32767.0f;
            return std::clamp(static_cast<float>(GInputState.gamepadAxes[static_cast<size_t>(axis)]) * inverseAxisMax,
                              -1.0f,
                              1.0f);
        }

        // --- audio -------------------------------------------------------------------------------

        void Audio_PlaySfx(GkStr path, float volume)
        {
            if (auto* engine = NextEngine::GetInstance(); engine != nullptr && engine->GetAudio() != nullptr)
            {
                engine->GetAudio()->PlaySfx(ToString(path), volume);
            }
        }

        void Audio_PlaySfxEx(GkStr path, float volume, uint32_t minIntervalMs)
        {
            if (auto* engine = NextEngine::GetInstance(); engine != nullptr && engine->GetAudio() != nullptr)
            {
                engine->GetAudio()->PlaySfx(ToString(path), volume, minIntervalMs);
            }
        }

        void Audio_PlayMusic(GkStr path, float volume)
        {
            if (auto* engine = NextEngine::GetInstance(); engine != nullptr && engine->GetAudio() != nullptr)
            {
                engine->GetAudio()->PlayMusic(ToString(path), volume);
            }
        }

        void Audio_StopMusic()
        {
            if (auto* engine = NextEngine::GetInstance(); engine != nullptr && engine->GetAudio() != nullptr)
            {
                engine->GetAudio()->StopMusic();
            }
        }

        void Audio_SetMusicVolume(float volume)
        {
            if (auto* engine = NextEngine::GetInstance(); engine != nullptr && engine->GetAudio() != nullptr)
            {
                engine->GetAudio()->SetMusicVolume(std::clamp(volume, 0.0f, 1.0f));
            }
        }

        // --- physics -----------------------------------------------------------------------------

        GkBool Physics_IsAvailable()
        {
            return GetPhysicsEngine() != nullptr ? 1 : 0;
        }

        void Physics_SetWorldPaused(GkBool paused)
        {
            if (NextPhysics* physics = GetPhysicsEngine())
            {
                physics->SetPaused(paused != 0);
            }
        }

        GkBool Physics_IsWorldPaused()
        {
            if (const NextPhysics* physics = GetPhysicsEngine())
            {
                return physics->IsPaused() ? 1 : 0;
            }
            return 0;
        }

        uint32_t Physics_CreateSphereBody(const FVec3* position, float radius, GkPhysicsMotionType motionType)
        {
            NextPhysics* physics = GetPhysicsEngine();
            NextMotionType nativeMotion{};
            const glm::vec3 nativePosition = ToGlm(position);
            if (!physics || position == nullptr || !IsFinite(nativePosition) || !std::isfinite(radius) ||
                radius <= 0.0f || !TryConvertMotionType(motionType, nativeMotion))
            {
                return NextBodyID::invalidValue;
            }
            return physics->CreateSphereBody(nativePosition, radius, nativeMotion).Value();
        }

        uint32_t Physics_CreateBoxBody(const FVec3* position, const FVec4* rotation, const FVec3* extent,
                                       GkPhysicsMotionType motionType)
        {
            NextPhysics* physics = GetPhysicsEngine();
            NextMotionType nativeMotion{};
            const glm::vec3 nativePosition = ToGlm(position);
            const glm::vec3 nativeExtent = glm::abs(ToGlm(extent));
            if (!physics || position == nullptr || rotation == nullptr || extent == nullptr ||
                !IsFinite(nativePosition) || !IsFinite(nativeExtent) ||
                glm::any(glm::lessThanEqual(nativeExtent, glm::vec3(0.0f))) ||
                !TryConvertMotionType(motionType, nativeMotion))
            {
                return NextBodyID::invalidValue;
            }
            return physics->CreateBoxBody(nativePosition, ToGlmQuat(rotation), nativeExtent, nativeMotion).Value();
        }

        void Physics_RemoveBody(uint32_t bodyId)
        {
            if (NextPhysics* physics = GetPhysicsEngine(); HasPhysicsBody(physics, bodyId))
            {
                physics->RemoveBody(ToBodyId(bodyId));
            }
        }

        void Physics_SetBodyActive(uint32_t bodyId, GkBool active)
        {
            if (NextPhysics* physics = GetPhysicsEngine(); HasPhysicsBody(physics, bodyId))
            {
                physics->SetBodyActive(ToBodyId(bodyId), active != 0);
            }
        }

        void Physics_SetBodyTransform(uint32_t bodyId, const FVec3* position, const FVec4* rotation,
                                      GkBool resetVelocity)
        {
            const glm::vec3 nativePosition = ToGlm(position);
            if (NextPhysics* physics = GetPhysicsEngine(); position != nullptr && rotation != nullptr &&
                IsFinite(nativePosition) && HasPhysicsBody(physics, bodyId))
            {
                physics->SetBodyTransform(ToBodyId(bodyId), nativePosition, ToGlmQuat(rotation), resetVelocity != 0);
            }
        }

        void Physics_MoveKinematicBody(uint32_t bodyId, const FVec3* position, const FVec4* rotation,
                                       float deltaSeconds)
        {
            const glm::vec3 nativePosition = ToGlm(position);
            if (NextPhysics* physics = GetPhysicsEngine(); position != nullptr && rotation != nullptr &&
                IsFinite(nativePosition) && std::isfinite(deltaSeconds) && deltaSeconds > 0.0f &&
                HasPhysicsBody(physics, bodyId))
            {
                physics->MoveKinematicBody(ToBodyId(bodyId), nativePosition, ToGlmQuat(rotation), deltaSeconds);
            }
        }

        void Physics_SetBodyVelocity(uint32_t bodyId, const FVec3* linearVelocity, const FVec3* angularVelocity)
        {
            const glm::vec3 linear = ToGlm(linearVelocity);
            const glm::vec3 angular = ToGlm(angularVelocity);
            if (NextPhysics* physics = GetPhysicsEngine(); linearVelocity != nullptr && angularVelocity != nullptr &&
                IsFinite(linear) && IsFinite(angular) && HasPhysicsBody(physics, bodyId))
            {
                physics->SetBodyVelocity(ToBodyId(bodyId), linear, angular);
            }
        }

        void Physics_AddForceToBody(uint32_t bodyId, const FVec3* force)
        {
            const glm::vec3 nativeForce = ToGlm(force);
            if (NextPhysics* physics = GetPhysicsEngine(); force != nullptr && IsFinite(nativeForce) &&
                HasPhysicsBody(physics, bodyId))
            {
                physics->AddForceToBody(ToBodyId(bodyId), nativeForce);
            }
        }

        void Physics_GetBodyState(uint32_t bodyId, FPhysicsBodyState* outState)
        {
            if (outState == nullptr)
            {
                return;
            }
            *outState = {};
            outState->Rotation.W = 1.0f;

            NextPhysics* physics = GetPhysicsEngine();
            FNextPhysicsBody* body = physics ? physics->GetBody(ToBodyId(bodyId)) : nullptr;
            if (!body)
            {
                return;
            }

            StoreVec3(outState->Position, body->position);
            StoreQuat(outState->Rotation, body->rotation);
            StoreVec3(outState->LinearVelocity, body->velocity);
            outState->MotionType = ConvertMotionType(body->motionType);
            outState->Active = physics->GetBodyDebugState(ToBodyId(bodyId)).isActive ? 1 : 0;
            outState->Valid = 1;
        }

        // --- immediate-mode UI --------------------------------------------------------------------

        GkBool UI_Begin(GkStr name, int32_t flags)
        {
            return ImGui::Begin(ToString(name).c_str(), nullptr, static_cast<ImGuiWindowFlags>(flags)) ? 1 : 0;
        }

        void UI_End() { ImGui::End(); }

        void UI_Text(GkStr text) { ImGui::TextUnformatted(ToString(text).c_str()); }

        void UI_SetCursorPos(float x, float y) { ImGui::SetCursorPos(ImVec2(x, y)); }

        void UI_GetWindowSize(FVec2* outSize)
        {
            const ImVec2 size = ImGui::GetWindowSize();
            StoreVec2(outSize, size.x, size.y);
        }

        void UI_SetWindowFontScale(float scale) { ImGui::SetWindowFontScale(scale); }

        void UI_GetScreenSize(FVec2* outSize)
        {
            // The ImGui viewport, not the swapchain extent. Managed code uses this to lay out what
            // it draws with UI_DrawText and UI_DrawRect, and those are in ImGui's coordinate space
            // — on a DPI-scaled display the two differ by the scale factor, which put every
            // centred HUD element off-centre by exactly that ratio.
            if (const ImGuiViewport* viewport = ImGui::GetMainViewport())
            {
                StoreVec2(outSize, viewport->Size.x, viewport->Size.y);
                return;
            }
            StoreVec2(outSize, 0.0f, 0.0f);
        }

        void UI_CalcTextSize(GkStr text, float scale, FVec2* outSize)
        {
            const ImVec2 size = ImGui::CalcTextSize(ToString(text).c_str());
            StoreVec2(outSize, size.x * scale, size.y * scale);
        }

        void UI_DrawText(GkStr text, float x, float y, GkColor32 color, float scale)
        {
            ImDrawList* drawList = ImGui::GetForegroundDrawList();
            if (drawList == nullptr)
            {
                return;
            }
            drawList->AddText(nullptr,
                              ImGui::GetFontSize() * std::max(scale, 0.01f),
                              ImVec2(x, y),
                              color,
                              ToString(text).c_str());
        }

        void UI_DrawRectFilled(float x, float y, float width, float height, GkColor32 color, float rounding)
        {
            if (ImDrawList* drawList = ImGui::GetForegroundDrawList())
            {
                drawList->AddRectFilled(ImVec2(x, y), ImVec2(x + width, y + height), color, rounding);
            }
        }

        void UI_DrawRect(float x, float y, float width, float height, GkColor32 color, float rounding,
                         float thickness)
        {
            if (ImDrawList* drawList = ImGui::GetForegroundDrawList())
            {
                drawList->AddRect(ImVec2(x, y), ImVec2(x + width, y + height), color, rounding, 0, thickness);
            }
        }

        void UI_RequestTexture(GkStr path, GkBool srgb, GkBool persistent, FUiTexture* outTexture)
        {
            if (outTexture == nullptr)
            {
                return;
            }
            *outTexture = FUiTexture{};
            auto* engine = NextEngine::GetInstance();
            NextUI::IUserInterface* ui = engine != nullptr ? engine->GetUserInterface() : nullptr;
            if (ui == nullptr)
            {
                return;
            }
            const NextUI::FUiTextureHandle texture = ui->RequestUiTexture(
                ToString(path),
                srgb != 0,
                persistent != 0 ? NextUI::EUiTextureLifetime::Persistent : NextUI::EUiTextureLifetime::Transient);
            outTexture->Handle = static_cast<uint64_t>(texture.textureId);
            outTexture->PixelSize = FVec2{texture.pixelSize.x, texture.pixelSize.y};
            outTexture->Valid = texture.valid ? 1 : 0;
        }

        bool IsValidUiRange(int32_t offset, int32_t count, int32_t total)
        {
            return offset >= 0 && count >= 0 && offset <= total && count <= total - offset;
        }

        void UI_SubmitDrawList(const FUiDrawCommand* commands,
                               int32_t commandCount,
                               const FVec2* points,
                               int32_t pointCount,
                               const uint8_t* utf8,
                               int32_t utf8Length)
        {
            constexpr int32_t maxCommands = 32768;
            constexpr int32_t maxPoints = 131072;
            constexpr int32_t maxTextBytes = 4 * 1024 * 1024;
            if (ImGui::GetCurrentContext() == nullptr ||
                commands == nullptr || commandCount <= 0 || commandCount > maxCommands ||
                pointCount < 0 || pointCount > maxPoints || utf8Length < 0 || utf8Length > maxTextBytes ||
                (pointCount > 0 && points == nullptr) || (utf8Length > 0 && utf8 == nullptr))
            {
                return;
            }

            static_assert(sizeof(FVec2) == sizeof(ImVec2));
            static_assert(alignof(FVec2) == alignof(ImVec2));
            const auto* drawPoints = reinterpret_cast<const ImVec2*>(points);

            for (int32_t index = 0; index < commandCount; ++index)
            {
                const FUiDrawCommand& command = commands[index];
                ImDrawList* drawList = command.Layer == static_cast<int32_t>(EUiDrawLayer::Background)
                                           ? ImGui::GetBackgroundDrawList()
                                           : ImGui::GetForegroundDrawList();
                if (drawList == nullptr)
                {
                    continue;
                }

                const ImVec2 p0(command.P0.X, command.P0.Y);
                const ImVec2 p1(command.P1.X, command.P1.Y);
                const float thickness = std::max(0.1f, command.Thickness);
                switch (static_cast<EUiDrawCommandType>(command.Type))
                {
                case EUiDrawCommandType::Line:
                    drawList->AddLine(p0, p1, command.Color, thickness);
                    break;
                case EUiDrawCommandType::Rect:
                    drawList->AddRect(p0, p1, command.Color, std::max(0.0f, command.Rounding), 0, thickness);
                    break;
                case EUiDrawCommandType::RectFilled:
                    drawList->AddRectFilled(p0, p1, command.Color, std::max(0.0f, command.Rounding));
                    break;
                case EUiDrawCommandType::Circle:
                    drawList->AddCircle(p0,
                                        std::max(0.0f, command.Radius),
                                        command.Color,
                                        std::clamp(command.Flags, 0, 128),
                                        thickness);
                    break;
                case EUiDrawCommandType::CircleFilled:
                    drawList->AddCircleFilled(p0,
                                              std::max(0.0f, command.Radius),
                                              command.Color,
                                              std::clamp(command.Flags, 0, 128));
                    break;
                case EUiDrawCommandType::Polyline:
                    if (IsValidUiRange(command.DataOffset, command.DataCount, pointCount) && command.DataCount >= 2)
                    {
                        drawList->AddPolyline(drawPoints + command.DataOffset,
                                              command.DataCount,
                                              command.Color,
                                              (command.Flags & 1) != 0 ? ImDrawFlags_Closed : ImDrawFlags_None,
                                              thickness);
                    }
                    break;
                case EUiDrawCommandType::ConvexPolyFilled:
                    if (IsValidUiRange(command.DataOffset, command.DataCount, pointCount) && command.DataCount >= 3)
                    {
                        drawList->AddConvexPolyFilled(drawPoints + command.DataOffset,
                                                      command.DataCount,
                                                      command.Color);
                    }
                    break;
                case EUiDrawCommandType::Text:
                    if (IsValidUiRange(command.DataOffset, command.DataCount, utf8Length) && command.DataCount > 0)
                    {
                        const char* begin = reinterpret_cast<const char*>(utf8 + command.DataOffset);
                        drawList->AddText(nullptr,
                                          ImGui::GetFontSize() * std::max(0.01f, command.Scale),
                                          p0,
                                          command.Color,
                                          begin,
                                          begin + command.DataCount);
                    }
                    break;
                case EUiDrawCommandType::Image:
                    if (command.TextureHandle != 0)
                    {
                        drawList->AddImage(static_cast<ImTextureID>(command.TextureHandle),
                                           p0,
                                           p1,
                                           ImVec2(command.Uv0.X, command.Uv0.Y),
                                           ImVec2(command.Uv1.X, command.Uv1.Y),
                                           command.Color);
                    }
                    break;
                default:
                    break;
                }
            }
        }

        // --- live scene ---------------------------------------------------------------------------

        uint32_t Scene_GetIndicesCount()
        {
            auto* engine = NextEngine::GetInstance();
            return engine != nullptr ? engine->GetScene().GetIndicesCount() : 0;
        }

        uint32_t Scene_FindNodeIdWithComponent(GkStr componentType)
        {
            auto* engine = NextEngine::GetInstance();
            if (engine == nullptr)
            {
                return GK_INVALID_NODE_ID;
            }
            const int32_t found = engine->GetScene().FindNodeIdWithComponent(ToString(componentType));
            return found < 0 ? GK_INVALID_NODE_ID : static_cast<uint32_t>(found);
        }

        uint32_t Scene_AddRenderNode(GkStr name, const FRenderNodeSpec* spec)
        {
            auto* engine = NextEngine::GetInstance();
            if (engine == nullptr || spec == nullptr)
            {
                return GK_INVALID_NODE_ID;
            }

            Assets::Scene& scene = engine->GetScene();
            if (spec->ModelId >= scene.Models().size())
            {
                SPDLOG_WARN("[dotnet] Scene.AddRenderNode got modelId {} but the scene has {} models",
                            spec->ModelId, scene.Models().size());
                return GK_INVALID_NODE_ID;
            }

            auto node = Assets::SceneBuilder::CreateRenderNode(ToString(name),
                                                              ToGlm(&spec->Translation),
                                                              ToGlm(&spec->Scale),
                                                              scene.GenerateInstanceId(),
                                                              spec->ModelId,
                                                              spec->MaterialId,
                                                              spec->Visible != 0,
                                                              ToGlmQuat(&spec->Rotation));
            if (!node)
            {
                return GK_INVALID_NODE_ID;
            }

            const uint32_t nodeId = node->GetInstanceId();
            scene.AddNode(node);
            scene.MarkDirty();
            return nodeId;
        }

        void Scene_RemoveNodeById(uint32_t nodeId)
        {
            if (auto* engine = NextEngine::GetInstance())
            {
                engine->GetScene().RemoveNodeByInstanceId(nodeId);
                engine->GetScene().MarkDirty();
            }
        }

        void Scene_MarkTransformDirty()
        {
            if (auto* engine = NextEngine::GetInstance())
            {
                engine->GetScene().MarkTransformDirty();
            }
        }

        void Scene_RecalcNodeTransform(uint32_t nodeId, GkBool full)
        {
            auto* engine = NextEngine::GetInstance();
            if (engine == nullptr)
            {
                return;
            }
            if (Assets::Node* node = engine->GetScene().GetNodeByInstanceId(nodeId))
            {
                node->RecalcTransform(full != 0);
            }
        }

        /// Reports an unknown node id once per id. Silently ignoring it is how a script ends up
        /// looking correct while nothing moves on screen.
        Assets::Node* FindNodeOrWarn(uint32_t nodeId, const char* what)
        {
            auto* engine = NextEngine::GetInstance();
            if (engine == nullptr)
            {
                return nullptr;
            }
            Assets::Node* node = engine->GetScene().GetNodeByInstanceId(nodeId);
            if (node == nullptr)
            {
                static std::unordered_set<uint32_t> reported;
                if (reported.insert(nodeId).second)
                {
                    SPDLOG_WARN("[dotnet] {} called with unknown node id {}", what, nodeId);
                }
            }
            return node;
        }

        void Scene_SetNodeTranslation(uint32_t nodeId, const FVec3* translation)
        {
            if (translation == nullptr)
            {
                return;
            }
            if (Assets::Node* node = FindNodeOrWarn(nodeId, "Scene.SetNodeTranslation"))
            {
                node->SetTranslation(ToGlm(translation));
            }
        }

        void Scene_SetNodeRotation(uint32_t nodeId, const FVec4* rotation)
        {
            if (rotation == nullptr)
            {
                return;
            }
            if (Assets::Node* node = FindNodeOrWarn(nodeId, "Scene.SetNodeRotation"))
            {
                node->SetRotation(ToGlmQuat(rotation));
            }
        }

        void Scene_SetNodeScale(uint32_t nodeId, const FVec3* scale)
        {
            if (scale == nullptr)
            {
                return;
            }
            if (Assets::Node* node = FindNodeOrWarn(nodeId, "Scene.SetNodeScale"))
            {
                node->SetScale(ToGlm(scale));
            }
        }

        void ApplyNodeTransform(const FNodeTransform& transform, const char* what)
        {
            if (Assets::Node* node = FindNodeOrWarn(transform.NodeId, what))
            {
                node->SetTransform(ToGlm(&transform.Translation),
                                   ToGlmQuat(&transform.Rotation),
                                   ToGlm(&transform.Scale));
            }
        }

        void Scene_SetNodeTransform(const FNodeTransform* transform)
        {
            if (transform != nullptr)
            {
                ApplyNodeTransform(*transform, "Scene.SetNodeTransform");
            }
        }

        void Scene_SetNodeTransforms(const FNodeTransform* transforms, int32_t transformCount)
        {
            constexpr int32_t maxTransformsPerCall = 1 << 20;
            if (transforms == nullptr || transformCount <= 0 || transformCount > maxTransformsPerCall)
            {
                return;
            }
            for (int32_t index = 0; index < transformCount; ++index)
            {
                ApplyNodeTransform(transforms[index], "Scene.SetNodeTransforms");
            }
        }

        void Scene_SetNodeVisible(uint32_t nodeId, GkBool visible)
        {
            // Visibility lives on the render component, not the node, so it goes through the
            // component rather than a node setter.
            if (Assets::Node* node = FindNodeOrWarn(nodeId, "Scene.SetNodeVisible"))
            {
                if (auto render = node->GetComponent<Runtime::RenderComponent>())
                {
                    render->SetVisible(visible != 0);
                }
            }
        }

        void Scene_SetNodeVisibleRecursive(uint32_t nodeId, GkBool visible)
        {
            if (auto* engine = NextEngine::GetInstance())
            {
                Assets::NodeUtils::SetVisibleRecursive(engine->GetScene().GetNodeSharedByInstanceId(nodeId), visible != 0);
            }
        }

        GkBool Scene_SetNodeParent(uint32_t childId, uint32_t parentId)
        {
            auto* engine = NextEngine::GetInstance();
            if (engine == nullptr)
            {
                return 0;
            }
            auto child = engine->GetScene().GetNodeSharedByInstanceId(childId);
            if (!child)
            {
                (void)FindNodeOrWarn(childId, "Scene.SetNodeParent");
                return 0;
            }
            try
            {
                if (parentId == GK_INVALID_NODE_ID)
                {
                    child->ClearParent();
                    return 1;
                }
                auto parent = engine->GetScene().GetNodeSharedByInstanceId(parentId);
                if (!parent)
                {
                    (void)FindNodeOrWarn(parentId, "Scene.SetNodeParent");
                    return 0;
                }
                child->SetParent(parent);
                return 1;
            }
            catch (const std::exception& exception)
            {
                SPDLOG_WARN("[dotnet] Scene.SetNodeParent({}, {}) failed: {}", childId, parentId, exception.what());
                return 0;
            }
        }

        void Scene_SetNodePrimaryMaterial(uint32_t nodeId, uint32_t materialId)
        {
            if (auto* engine = NextEngine::GetInstance())
            {
                Assets::NodeUtils::SetPrimaryMaterial(engine->GetScene().GetNodeSharedByInstanceId(nodeId), materialId);
            }
        }

        void Scene_SetNodeMaterialRecursive(uint32_t nodeId, uint32_t materialId)
        {
            if (auto* engine = NextEngine::GetInstance())
            {
                Assets::NodeUtils::SetMaterialRecursive(engine->GetScene().GetNodeSharedByInstanceId(nodeId), materialId);
            }
        }

        void Scene_SetNodeOutlineFlags(uint32_t nodeId, uint32_t outlineFlags)
        {
            if (auto* engine = NextEngine::GetInstance())
            {
                Assets::NodeUtils::SetOutlineFlags(engine->GetScene().GetNodeSharedByInstanceId(nodeId), outlineFlags);
            }
        }

        uint32_t Scene_AddLambertianMaterial(const FVec3* color)
        {
            auto* engine = NextEngine::GetInstance();
            return engine != nullptr
                       ? Assets::SceneBuilder::AddLambertianMaterialToScene(engine->GetScene(), ToGlm(color))
                       : 0;
        }

        uint32_t Scene_AddDiffuseLightMaterial(const FVec3* color, float intensity)
        {
            auto* engine = NextEngine::GetInstance();
            return engine != nullptr
                       ? Assets::SceneBuilder::AddDiffuseLightMaterialToScene(engine->GetScene(), ToGlm(color), intensity)
                       : 0;
        }

        GkBool Scene_BindPhysicsBody(uint32_t nodeId, uint32_t bodyId, GkNodeMobility mobility)
        {
            NextEngine* engine = NextEngine::GetInstance();
            Runtime::ENodeMobility nativeMobility{};
            if (!engine || !HasPhysicsBody(engine->GetPhysicsEngine(), bodyId) ||
                !TryConvertNodeMobility(mobility, nativeMobility))
            {
                return 0;
            }
            Assets::Node* node = FindNodeOrWarn(nodeId, "Scene.BindPhysicsBody");
            return node && engine->GetScene().BindPhysicsBody(*node, ToBodyId(bodyId), nativeMobility) ? 1 : 0;
        }

        uint32_t Scene_GetEnvironmentNodeId()
        {
            auto* engine = NextEngine::GetInstance();
            if (engine == nullptr)
            {
                return GK_INVALID_NODE_ID;
            }

            Assets::Scene& scene = engine->GetScene();
            // Creates the node when the scene has none, which is the case for every procedurally
            // built scene. The return value is the settings, not the node, so the id is looked up
            // afterwards rather than reached through the scene's private component cache.
            scene.GetEnvSettings();

            const int32_t found = scene.FindNodeIdWithComponent("EnvironmentComponent");
            return found < 0 ? GK_INVALID_NODE_ID : static_cast<uint32_t>(found);
        }

        // --- scene construction ---------------------------------------------------------------------

        uint32_t SceneBuild_AddBoxModel(const FVec3* min, const FVec3* max)
        {
            if (!RequireSceneBuild("SceneBuild.AddBoxModel"))
            {
                return 0;
            }
            GSceneBuildContext.models->push_back(Assets::FProcModel::CreateBox(ToGlm(min), ToGlm(max)));
            return static_cast<uint32_t>(GSceneBuildContext.models->size() - 1);
        }

        uint32_t SceneBuild_AddSphereModel(const FVec3* center, float radius)
        {
            if (!RequireSceneBuild("SceneBuild.AddSphereModel"))
            {
                return 0;
            }
            GSceneBuildContext.models->push_back(
                Assets::FProcModel::CreateSphere(ToGlm(center), std::max(0.001f, radius)));
            return static_cast<uint32_t>(GSceneBuildContext.models->size() - 1);
        }

        uint32_t SceneBuild_AddLambertianMaterial(const FVec3* color)
        {
            if (!RequireSceneBuild("SceneBuild.AddLambertianMaterial"))
            {
                return 0;
            }
            return Assets::SceneBuilder::AddLambertianMaterial(*GSceneBuildContext.materials, ToGlm(color));
        }

        uint32_t SceneBuild_AddDiffuseLightMaterial(const FVec3* color, float intensity)
        {
            if (!RequireSceneBuild("SceneBuild.AddDiffuseLightMaterial"))
            {
                return 0;
            }
            return Assets::SceneBuilder::AddDiffuseLightMaterial(*GSceneBuildContext.materials, ToGlm(color),
                                                                intensity);
        }

        uint32_t SceneBuild_AddRenderNode(GkStr name, const FRenderNodeSpec* spec)
        {
            if (!RequireSceneBuild("SceneBuild.AddRenderNode") || spec == nullptr)
            {
                return GK_INVALID_NODE_ID;
            }
            if (spec->ModelId >= GSceneBuildContext.models->size())
            {
                SPDLOG_WARN("[dotnet] SceneBuild.AddRenderNode got modelId {} but only {} models were built",
                            spec->ModelId, GSceneBuildContext.models->size());
                return GK_INVALID_NODE_ID;
            }

            uint32_t instanceId = 0;
            for (const auto& existing : *GSceneBuildContext.nodes)
            {
                instanceId = std::max(instanceId, existing->GetInstanceId() + 1);
            }

            auto node = Assets::SceneBuilder::CreateRenderNode(ToString(name),
                                                              ToGlm(&spec->Translation),
                                                              ToGlm(&spec->Scale),
                                                              instanceId,
                                                              spec->ModelId,
                                                              spec->MaterialId,
                                                              spec->Visible != 0,
                                                              ToGlmQuat(&spec->Rotation));
            if (!node)
            {
                return GK_INVALID_NODE_ID;
            }
            const uint32_t nodeId = node->GetInstanceId();
            GSceneBuildContext.nodes->push_back(node);
            return nodeId;
        }

        GkBool SceneBuild_BindPhysicsBody(uint32_t nodeId, uint32_t bodyId, GkNodeMobility mobility)
        {
            if (!RequireSceneBuild("SceneBuild.BindPhysicsBody"))
            {
                return 0;
            }
            NextEngine* engine = NextEngine::GetInstance();
            Runtime::ENodeMobility nativeMobility{};
            if (!engine || !HasPhysicsBody(engine->GetPhysicsEngine(), bodyId) ||
                !TryConvertNodeMobility(mobility, nativeMobility))
            {
                return 0;
            }

            const auto found = std::ranges::find_if(*GSceneBuildContext.nodes, [nodeId](const auto& node)
            {
                return node && node->GetInstanceId() == nodeId;
            });
            if (found == GSceneBuildContext.nodes->end())
            {
                SPDLOG_WARN("[dotnet] SceneBuild.BindPhysicsBody could not find node id {}", nodeId);
                return 0;
            }
            return engine->GetScene().BindPhysicsBody(**found, ToBodyId(bodyId), nativeMobility) ? 1 : 0;
        }

        // --- component and node property access ------------------------------------------------------

        /// A reflected property lives either on a component attached to a node, or on the node
        /// itself. Both are (meta type, instance) pairs, so resolving them to one shape keeps the
        /// twenty accessors below free of that distinction.
        struct FPropertyTarget
        {
            entt::meta_type Type;
            void* Instance = nullptr;

            explicit operator bool() const { return Instance != nullptr && static_cast<bool>(Type); }
        };

        FPropertyTarget ResolvePropertyTarget(uint32_t nodeId, uint32_t typeId, const char* what)
        {
            Assets::Node* node = FindNodeOrWarn(nodeId, what);
            if (node == nullptr)
            {
                return {};
            }

            const entt::meta_type nodeType = entt::resolve<Assets::Node>();
            if (nodeType && nodeType.id() == typeId)
            {
                return FPropertyTarget{nodeType, node};
            }

            // A node carries a handful of components, so a linear scan beats maintaining a second
            // index keyed by meta id.
            for (const auto& component : node->GetComponents())
            {
                if (!component)
                {
                    continue;
                }
                const entt::meta_type type = component->GetMetaType();
                if (type && type.id() == typeId)
                {
                    return FPropertyTarget{type, component.get()};
                }
            }

            static std::unordered_set<uint64_t> reported;
            const uint64_t key = (static_cast<uint64_t>(nodeId) << 32) | typeId;
            if (reported.insert(key).second)
            {
                SPDLOG_WARN("[dotnet] {}: node {} has no component with type id {}", what, nodeId, typeId);
            }
            return {};
        }

        /// Reads a reflected property as T. Failure is a warning plus a zero value: a getter has no
        /// error channel across this ABI, and the alternative — returning uninitialised memory —
        /// would be worse than a visibly wrong value.
        template <typename T>
        T GetProperty(uint32_t nodeId, uint32_t typeId, uint32_t propId, const char* what)
        {
            const FPropertyTarget target = ResolvePropertyTarget(nodeId, typeId, what);
            if (!target)
            {
                return T{};
            }

            entt::meta_any value = Reflection::PropertyAccessor::GetPropertyValueById(
                target.Type, target.Instance, propId);
            if (!value)
            {
                return T{};
            }
            if (const T* typed = value.try_cast<T>())
            {
                return *typed;
            }
            // A mismatch here means the generated wrapper picked an accessor that does not match the
            // property's real type, i.e. the committed manifest is stale.
            SPDLOG_WARN("[dotnet] {}: property {} on node {} is not the expected type", what, propId, nodeId);
            return T{};
        }

        template <typename T>
        void SetProperty(uint32_t nodeId, uint32_t typeId, uint32_t propId, const T& value, const char* what)
        {
            const FPropertyTarget target = ResolvePropertyTarget(nodeId, typeId, what);
            if (!target)
            {
                return;
            }
            if (!Reflection::PropertyAccessor::SetPropertyValueById(target.Type, target.Instance, propId,
                                                                    entt::meta_any{value}))
            {
                SPDLOG_WARN("[dotnet] {}: could not write property {} on node {}", what, propId, nodeId);
            }
        }

        GkBool Component_Has(uint32_t nodeId, uint32_t typeId)
        {
            // Deliberately silent: asking whether a component is present is a question, not a
            // mistake, so this must not go through the warning path the accessors use.
            Assets::Node* node = nullptr;
            if (auto* engine = NextEngine::GetInstance())
            {
                node = engine->GetScene().GetNodeByInstanceId(nodeId);
            }
            if (node == nullptr)
            {
                return 0;
            }

            const entt::meta_type nodeType = entt::resolve<Assets::Node>();
            if (nodeType && nodeType.id() == typeId)
            {
                return 1;
            }
            for (const auto& component : node->GetComponents())
            {
                const entt::meta_type type = component ? component->GetMetaType() : entt::meta_type{};
                if (type && type.id() == typeId)
                {
                    return 1;
                }
            }
            return 0;
        }

        GkBool Component_GetBool(uint32_t nodeId, uint32_t typeId, uint32_t propId)
        {
            return GetProperty<bool>(nodeId, typeId, propId, "Component.GetBool") ? 1 : 0;
        }

        void Component_SetBool(uint32_t nodeId, uint32_t typeId, uint32_t propId, GkBool value)
        {
            SetProperty<bool>(nodeId, typeId, propId, value != 0, "Component.SetBool");
        }

        int32_t Component_GetInt32(uint32_t nodeId, uint32_t typeId, uint32_t propId)
        {
            return GetProperty<int32_t>(nodeId, typeId, propId, "Component.GetInt32");
        }

        void Component_SetInt32(uint32_t nodeId, uint32_t typeId, uint32_t propId, int32_t value)
        {
            SetProperty<int32_t>(nodeId, typeId, propId, value, "Component.SetInt32");
        }

        uint32_t Component_GetUInt32(uint32_t nodeId, uint32_t typeId, uint32_t propId)
        {
            return GetProperty<uint32_t>(nodeId, typeId, propId, "Component.GetUInt32");
        }

        void Component_SetUInt32(uint32_t nodeId, uint32_t typeId, uint32_t propId, uint32_t value)
        {
            SetProperty<uint32_t>(nodeId, typeId, propId, value, "Component.SetUInt32");
        }

        float Component_GetFloat(uint32_t nodeId, uint32_t typeId, uint32_t propId)
        {
            return GetProperty<float>(nodeId, typeId, propId, "Component.GetFloat");
        }

        void Component_SetFloat(uint32_t nodeId, uint32_t typeId, uint32_t propId, float value)
        {
            SetProperty<float>(nodeId, typeId, propId, value, "Component.SetFloat");
        }

        double Component_GetDouble(uint32_t nodeId, uint32_t typeId, uint32_t propId)
        {
            return GetProperty<double>(nodeId, typeId, propId, "Component.GetDouble");
        }

        void Component_SetDouble(uint32_t nodeId, uint32_t typeId, uint32_t propId, double value)
        {
            SetProperty<double>(nodeId, typeId, propId, value, "Component.SetDouble");
        }

        void Component_GetVec2(uint32_t nodeId, uint32_t typeId, uint32_t propId, FVec2* outValue)
        {
            const glm::vec2 value = GetProperty<glm::vec2>(nodeId, typeId, propId, "Component.GetVec2");
            StoreVec2(outValue, value.x, value.y);
        }

        void Component_SetVec2(uint32_t nodeId, uint32_t typeId, uint32_t propId, const FVec2* value)
        {
            if (value == nullptr)
            {
                return;
            }
            SetProperty<glm::vec2>(nodeId, typeId, propId, glm::vec2(value->X, value->Y), "Component.SetVec2");
        }

        void Component_GetVec3(uint32_t nodeId, uint32_t typeId, uint32_t propId, FVec3* outValue)
        {
            const glm::vec3 value = GetProperty<glm::vec3>(nodeId, typeId, propId, "Component.GetVec3");
            if (outValue != nullptr)
            {
                outValue->X = value.x;
                outValue->Y = value.y;
                outValue->Z = value.z;
            }
        }

        void Component_SetVec3(uint32_t nodeId, uint32_t typeId, uint32_t propId, const FVec3* value)
        {
            if (value == nullptr)
            {
                return;
            }
            SetProperty<glm::vec3>(nodeId, typeId, propId, ToGlm(value), "Component.SetVec3");
        }

        void Component_GetVec4(uint32_t nodeId, uint32_t typeId, uint32_t propId, FVec4* outValue)
        {
            const glm::vec4 value = GetProperty<glm::vec4>(nodeId, typeId, propId, "Component.GetVec4");
            if (outValue != nullptr)
            {
                outValue->X = value.x;
                outValue->Y = value.y;
                outValue->Z = value.z;
                outValue->W = value.w;
            }
        }

        void Component_SetVec4(uint32_t nodeId, uint32_t typeId, uint32_t propId, const FVec4* value)
        {
            if (value == nullptr)
            {
                return;
            }
            SetProperty<glm::vec4>(nodeId, typeId, propId, glm::vec4(value->X, value->Y, value->Z, value->W),
                                   "Component.SetVec4");
        }

        void Component_GetQuat(uint32_t nodeId, uint32_t typeId, uint32_t propId, FVec4* outValue)
        {
            const glm::quat value = GetProperty<glm::quat>(nodeId, typeId, propId, "Component.GetQuat");
            if (outValue != nullptr)
            {
                // glm::quat is (w, x, y, z) in memory; the managed side uses (x, y, z, w).
                outValue->X = value.x;
                outValue->Y = value.y;
                outValue->Z = value.z;
                outValue->W = value.w;
            }
        }

        void Component_SetQuat(uint32_t nodeId, uint32_t typeId, uint32_t propId, const FVec4* value)
        {
            if (value == nullptr)
            {
                return;
            }
            SetProperty<glm::quat>(nodeId, typeId, propId, glm::quat(value->W, value->X, value->Y, value->Z),
                                   "Component.SetQuat");
        }

        int32_t Component_GetString(uint32_t nodeId, uint32_t typeId, uint32_t propId, char* buffer, int32_t capacity)
        {
            return WriteString(GetProperty<std::string>(nodeId, typeId, propId, "Component.GetString"),
                               buffer, capacity);
        }

        void Component_SetString(uint32_t nodeId, uint32_t typeId, uint32_t propId, GkStr value)
        {
            SetProperty<std::string>(nodeId, typeId, propId, ToString(value), "Component.SetString");
        }

        // --- paths and asset I/O -------------------------------------------------------------------

        int32_t Paths_GetProjectRoot(char* buffer, int32_t capacity)
        {
            std::error_code ec;
            std::filesystem::path cursor = std::filesystem::current_path(ec);
            for (int depth = 0; depth < 8 && !ec; depth++)
            {
                if (std::filesystem::exists(cursor / "AGENTS.md", ec) &&
                    std::filesystem::exists(cursor / "assets", ec))
                {
                    return WriteString(cursor.string(), buffer, capacity);
                }
                if (!cursor.has_parent_path())
                {
                    break;
                }
                cursor = cursor.parent_path();
            }
            return WriteString(std::filesystem::current_path(ec).string(), buffer, capacity);
        }

        int32_t Paths_GetOutputDir(char* buffer, int32_t capacity)
        {
            std::error_code ec;
            return WriteString((std::filesystem::current_path(ec) / "out").string(), buffer, capacity);
        }

        int32_t Assets_ReadFile(GkStr path, uint8_t* buffer, int32_t capacity)
        {
            std::vector<uint8_t> contents;
            if (!Utilities::Package::FPackageFileSystem::GetInstance().LoadFile(ToString(path), contents))
            {
                return 0;
            }

            const int32_t size = static_cast<int32_t>(contents.size());
            if (buffer == nullptr || capacity <= 0)
            {
                return size;
            }
            const int32_t copied = std::min(size, capacity);
            std::memcpy(buffer, contents.data(), static_cast<size_t>(copied));
            return copied;
        }
    }

    FEngineApi BuildEngineApi()
    {
        FEngineApi api{};
        api.Version = GK_DOTNET_ABI_VERSION;

        // Expanded from the same def file that declares the struct, so a binding cannot be declared
        // without being filled in: a missing implementation is a compile error here, not a null
        // pointer discovered by managed code at runtime.
#define GK_API(ns, name, ret, params) api.ns##_##name = &ns##_##name;
#include "Modules/NextDotNet/EngineApi.def.h"
#undef GK_API

        return api;
    }
}
