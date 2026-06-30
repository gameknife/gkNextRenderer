#include "Engine/Runtime/AgentDriver/AgentDriver.hpp"

#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Options.hpp"
#include "Engine/Runtime/Config/CVarSystem.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/GameInstance.hpp"
#include "Engine/Runtime/Input/SyntheticInput.hpp"
#include "Engine/Utilities/FileHelper.hpp"
#include "Engine/Vulkan/WindowSurface.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <fmt/format.h>
#include <imgui.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

using json = nlohmann::json;

namespace Runtime::Agent
{
    namespace
    {
        std::string StatusToString(NextRenderer::EApplicationStatus status)
        {
            switch (status)
            {
            case NextRenderer::EApplicationStatus::Starting: return "Starting";
            case NextRenderer::EApplicationStatus::Running: return "Running";
            case NextRenderer::EApplicationStatus::Loading: return "Loading";
            case NextRenderer::EApplicationStatus::AsyncPreparing: return "AsyncPreparing";
            default: return "Unknown";
            }
        }

        json QueryValueToJson(const FAgentQueryValue& value)
        {
            if (std::holds_alternative<bool>(value))
            {
                return std::get<bool>(value);
            }
            if (std::holds_alternative<int64_t>(value))
            {
                return std::get<int64_t>(value);
            }
            if (std::holds_alternative<double>(value))
            {
                return std::get<double>(value);
            }
            return std::get<std::string>(value);
        }

        std::optional<double> ToNumber(const FAgentQueryValue& value)
        {
            try
            {
                if (std::holds_alternative<int64_t>(value))
                {
                    return static_cast<double>(std::get<int64_t>(value));
                }
                if (std::holds_alternative<double>(value))
                {
                    return std::get<double>(value);
                }
                if (std::holds_alternative<bool>(value))
                {
                    return std::get<bool>(value) ? 1.0 : 0.0;
                }
                return std::stod(std::get<std::string>(value));
            }
            catch (const std::exception&)
            {
                return std::nullopt;
            }
        }

        std::optional<double> ExpectedNumber(const json& value)
        {
            if (value.is_number())
            {
                return value.get<double>();
            }
            if (value.is_boolean())
            {
                return value.get<bool>() ? 1.0 : 0.0;
            }
            if (value.is_string())
            {
                try
                {
                    return std::stod(value.get<std::string>());
                }
                catch (const std::exception&)
                {
                    return std::nullopt;
                }
            }
            return std::nullopt;
        }

        bool IsAbsolutePath(const std::string& path)
        {
            return std::filesystem::path(path).is_absolute();
        }

        std::string StemFromPath(const std::string& path)
        {
            std::string stem = std::filesystem::path(path).stem().string();
            if (stem.empty())
            {
                return "agent_script";
            }
            return stem;
        }

        std::string JoinLabels(const std::vector<std::string>& values)
        {
            if (values.empty())
            {
                return "-";
            }
            std::string result;
            for (size_t i = 0; i < values.size(); ++i)
            {
                if (i > 0)
                {
                    result += " + ";
                }
                result += values[i];
            }
            return result;
        }

        float Clamp01(float value)
        {
            return std::clamp(value, 0.0f, 1.0f);
        }

        ImU32 WithAlpha(ImU32 color, float alpha)
        {
            const auto a = static_cast<ImU32>(Clamp01(alpha) * 255.0f);
            return (color & IM_COL32(255, 255, 255, 0)) | (a << IM_COL32_A_SHIFT);
        }
    }

    FAgentDriver::FAgentDriver(NextEngine& engine) : engine_(engine)
    {
        loaded_ = LoadScript();
        if (!loaded_)
        {
            exitCode_ = 3;
            engine_.RequestExit(exitCode_);
            closeRequested_ = true;
            return;
        }

        RegisterGameQueries();
        report_ = {
            {"name", name_},
            {"passed", true},
            {"steps", json::array()},
            {"screenshots", json::array()},
            {"exitCode", 0},
        };
        SPDLOG_INFO("[AgentDriver] loaded script '{}' -> report '{}'", name_, reportPath_);
    }

    FAgentDriver::~FAgentDriver()
    {
        if (loaded_ && !finished_)
        {
            WriteReport();
        }
    }

    bool FAgentDriver::LoadScript()
    {
        const auto& options = engine_.GetOptions();
        if (options.AgentScript.empty())
        {
            SPDLOG_ERROR("[AgentDriver] missing --agent-script");
            return false;
        }

        std::filesystem::path scriptPath(options.AgentScript);
        if (!scriptPath.is_absolute())
        {
            std::error_code ec;
            if (!std::filesystem::exists(scriptPath, ec))
            {
                scriptPath = Utilities::FileHelper::GetRuntimeRoot() / scriptPath;
            }
        }

        std::ifstream input(scriptPath, std::ios::binary);
        if (!input.is_open())
        {
            SPDLOG_ERROR("[AgentDriver] failed to open script '{}'", scriptPath.string());
            return false;
        }

        try
        {
            input >> script_;
        }
        catch (const std::exception& e)
        {
            SPDLOG_ERROR("[AgentDriver] invalid JSON script '{}': {}", scriptPath.string(), e.what());
            return false;
        }

        if (!script_.contains("steps") || !script_["steps"].is_array())
        {
            SPDLOG_ERROR("[AgentDriver] script '{}' does not contain a steps array", scriptPath.string());
            return false;
        }

        name_ = script_.value("name", StemFromPath(scriptPath.string()));
        if (script_.contains("defaults") && script_["defaults"].is_object())
        {
            const auto& defaults = script_["defaults"];
            defaultWaitFrames_ = defaults.value("waitFrames", defaultWaitFrames_);
            defaultStepTimeoutMs_ = defaults.value("stepTimeoutMs", defaultStepTimeoutMs_);
        }

        const std::string defaultReport = fmt::format("agent_reports/{}.json", name_);
        reportPath_ = ResolveOutputPath(options.AgentReport, defaultReport);
        return true;
    }

    void FAgentDriver::RegisterGameQueries()
    {
        if (auto* game = engine_.GetGameInstance())
        {
            game->RegisterAgentQueries(gameQueries_);
        }
    }

    void FAgentDriver::Tick(double)
    {
        if (!loaded_ || finished_ || closeRequested_)
        {
            return;
        }
        if (engine_.GetEngineStatus() != NextRenderer::EApplicationStatus::Running)
        {
            return;
        }

        const auto& steps = script_["steps"];
        int instantSteps = 0;
        while (!finished_ && !closeRequested_ && stepIndex_ < steps.size() && instantSteps++ < 32)
        {
            const json& step = steps[stepIndex_];
            SetCurrentStepLabel(step);
            if (wait_.kind != EWaitKind::None)
            {
                if (!TickWait(step))
                {
                    return;
                }
                wait_ = {};
                ++stepIndex_;
                continue;
            }

            if (!step.is_object())
            {
                RecordFailure(step, "step must be an object", true);
                return;
            }

            if (!ExecuteStep(step))
            {
                return;
            }

            if (wait_.kind == EWaitKind::None)
            {
                ++stepIndex_;
            }
        }

        if (!finished_ && !closeRequested_ && stepIndex_ >= steps.size())
        {
            Finish(failed_ ? 1 : 0);
        }
    }

    void FAgentDriver::Finish(int exitCode)
    {
        if (finished_ || closeRequested_)
        {
            return;
        }
        finished_ = true;
        exitCode_ = exitCode;
        report_["passed"] = exitCode == 0;
        report_["exitCode"] = exitCode;
        report_["framesRendered"] = engine_.GetTotalFrames();
        WriteReport();
        engine_.RequestExit(exitCode);
        closeRequested_ = true;
    }

    void FAgentDriver::DrawStatusOverlay() const
    {
        if (!loaded_)
        {
            return;
        }

        const auto& steps = script_["steps"];
        const size_t totalSteps = steps.is_array() ? steps.size() : 0;
        const double now = engine_.GetTime();
        const ImGuiIO& io = ImGui::GetIO();
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const ImVec2 viewportPos = viewport ? viewport->Pos : ImVec2(0.0f, 0.0f);
        const ImVec2 viewportSize = viewport ? viewport->Size : io.DisplaySize;
        ImDrawList* drawList = ImGui::GetForegroundDrawList();

        const ImVec2 pos(viewportPos.x + 12.0f, viewportPos.y + 36.0f);
        const ImVec2 size(260.0f * std::max(1.0f, io.FontGlobalScale), 0.0f);

        ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(size, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.58f);
        constexpr ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoFocusOnAppearing |
            ImGuiWindowFlags_NoNav |
            ImGuiWindowFlags_NoInputs |
            ImGuiWindowFlags_AlwaysAutoResize;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 5.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(9.0f, 7.0f));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.03f, 0.04f, 0.05f, 0.58f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.42f, 0.66f, 0.90f, 0.35f));
        if (ImGui::Begin("##AgentDriverStatusOverlay", nullptr, flags))
        {
            ImGui::TextColored(ImVec4(0.48f, 0.78f, 1.0f, 1.0f), "Agent");
            ImGui::SameLine();
            ImGui::TextDisabled("%zu/%zu  %s", std::min(stepIndex_ + 1, totalSteps), totalSteps, name_.c_str());
            if (!currentStepLabel_.empty())
            {
                ImGui::TextWrapped("%s", currentStepLabel_.c_str());
            }
            if (!activeKeys_.empty() || !activeMouseButtons_.empty())
            {
                ImGui::TextDisabled("held: %s / %s", JoinLabels(activeKeys_).c_str(), JoinLabels(activeMouseButtons_).c_str());
            }
            if (failed_)
            {
                ImGui::TextColored(ImVec4(1.0f, 0.34f, 0.28f, 1.0f), "FAILED");
            }
        }
        ImGui::End();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(2);

        const float mouseAge = static_cast<float>(now - lastMouseActionTime_);
        const bool dragging = !activeMouseButtons_.empty();
        const bool showMouse = dragging || mouseAge < 1.35f;
        if (showMouse)
        {
            const float alpha = dragging ? 1.0f : (1.0f - Clamp01(mouseAge / 1.35f));
            const ImVec2 marker(viewportPos.x + lastMousePos_.x, viewportPos.y + lastMousePos_.y);
            const float radius = dragging ? 11.0f : 9.0f;
            drawList->AddCircleFilled(marker, radius, WithAlpha(IM_COL32(64, 176, 255, 255), 0.44f * alpha), 32);
            drawList->AddCircle(marker, radius + 3.0f, WithAlpha(IM_COL32(255, 255, 255, 255), 0.88f * alpha), 32, 2.0f);
            drawList->AddCircle(marker, radius + 8.0f, WithAlpha(IM_COL32(64, 176, 255, 255), 0.34f * alpha), 32, 1.5f);
            if (dragging)
            {
                const std::string label = JoinLabels(activeMouseButtons_);
                const ImVec2 textSize = ImGui::CalcTextSize(label.c_str());
                const ImVec2 textPos(marker.x + 15.0f, marker.y - textSize.y * 0.5f);
                drawList->AddRectFilled(ImVec2(textPos.x - 6.0f, textPos.y - 3.0f),
                                        ImVec2(textPos.x + textSize.x + 6.0f, textPos.y + textSize.y + 3.0f),
                                        WithAlpha(IM_COL32(8, 10, 12, 255), 0.62f * alpha), 4.0f);
                drawList->AddText(textPos, WithAlpha(IM_COL32(255, 255, 255, 255), alpha), label.c_str());
            }
        }

        const float dragAge = static_cast<float>(now - lastDragActionTime_);
        if (dragAge < 1.65f)
        {
            const float alpha = 1.0f - Clamp01(dragAge / 1.65f);
            const ImVec2 from(viewportPos.x + dragStart_.x, viewportPos.y + dragStart_.y);
            const ImVec2 to(viewportPos.x + dragEnd_.x, viewportPos.y + dragEnd_.y);
            drawList->AddLine(from, to, WithAlpha(IM_COL32(255, 208, 96, 255), 0.78f * alpha), 4.0f);
            drawList->AddCircleFilled(from, 5.0f, WithAlpha(IM_COL32(255, 208, 96, 255), 0.70f * alpha), 24);
            drawList->AddCircleFilled(to, 7.0f, WithAlpha(IM_COL32(255, 255, 255, 255), 0.82f * alpha), 24);
        }

        const float keyboardAge = static_cast<float>(now - lastKeyboardActionTime_);
        if (!keyboardToast_.empty() && keyboardAge < 1.25f)
        {
            const float alpha = 1.0f - Clamp01((keyboardAge - 0.85f) / 0.40f);
            const ImVec2 textSize = ImGui::CalcTextSize(keyboardToast_.c_str());
            const ImVec2 center(viewportPos.x + viewportSize.x * 0.5f, viewportPos.y + viewportSize.y * 0.42f);
            const ImVec2 pad(18.0f, 11.0f);
            const ImVec2 min(center.x - textSize.x * 0.5f - pad.x, center.y - textSize.y * 0.5f - pad.y);
            const ImVec2 max(center.x + textSize.x * 0.5f + pad.x, center.y + textSize.y * 0.5f + pad.y);
            drawList->AddRectFilled(min, max, WithAlpha(IM_COL32(10, 12, 15, 255), 0.74f * alpha), 7.0f);
            drawList->AddRect(min, max, WithAlpha(IM_COL32(125, 205, 255, 255), 0.76f * alpha), 7.0f, 0, 1.5f);
            drawList->AddText(ImVec2(center.x - textSize.x * 0.5f, center.y - textSize.y * 0.5f),
                              WithAlpha(IM_COL32(255, 255, 255, 255), alpha), keyboardToast_.c_str());
        }
    }

    void FAgentDriver::WriteReport()
    {
        try
        {
            const std::filesystem::path path(reportPath_);
            Utilities::FileHelper::EnsureDirectoryExists(path.parent_path());
            std::ofstream output(path, std::ios::binary);
            output << report_.dump(2);
            SPDLOG_INFO("[AgentDriver] report -> {}", reportPath_);
        }
        catch (const std::exception& e)
        {
            SPDLOG_ERROR("[AgentDriver] failed to write report '{}': {}", reportPath_, e.what());
        }
    }

    void FAgentDriver::RecordStep(json entry)
    {
        entry["idx"] = stepIndex_;
        report_["steps"].push_back(std::move(entry));
    }

    void FAgentDriver::RecordFailure(const json& step, std::string message, bool fatal)
    {
        failed_ = true;
        const std::string failureStem = ResolveOutputPath(
            "",
            fmt::format("screenshots/{}_FAIL_{:03}", name_, stepIndex_));
        Utilities::FileHelper::EnsureDirectoryExists(std::filesystem::path(failureStem).parent_path());
        engine_.RequestScreenShot({.filename = failureStem, .sync = true, .includeUi = engine_.GetOptions().AgentValidationUI});

        json entry = {
            {"type", step.value("type", "<unknown>")},
            {"passed", false},
            {"message", std::move(message)},
            {"screenshot", failureStem + ".jpg"},
        };
        report_["screenshots"].push_back(failureStem + ".jpg");
        RecordStep(std::move(entry));
        if (fatal)
        {
            Finish(1);
        }
    }

    bool FAgentDriver::TickWait(const json& step)
    {
        if (wait_.kind == EWaitKind::Frames)
        {
            if (engine_.GetTotalFrames() < wait_.targetFrame)
            {
                return false;
            }
            RecordStep({{"type", "wait-frames"},
                        {"n", wait_.targetFrame - wait_.startFrame},
                        {"passed", true}});
            return true;
        }
        if (wait_.kind == EWaitKind::Milliseconds)
        {
            if (engine_.GetTime() < wait_.targetTime)
            {
                return false;
            }
            RecordStep({{"type", "wait-ms"},
                        {"ms", static_cast<int>((wait_.targetTime - wait_.startTime) * 1000.0)},
                        {"passed", true}});
            return true;
        }
        if (wait_.kind == EWaitKind::Screenshot)
        {
            if (engine_.IsCapturingScreenShot() || engine_.GetTotalFrames() < wait_.startFrame + 2)
            {
                return false;
            }
            json entry = {
                {"type", "screenshot"},
                {"passed", true},
                {"out", lastScreenshotPath_ + ".jpg"},
            };
            report_["screenshots"].push_back(lastScreenshotPath_ + ".jpg");
            RecordStep(std::move(entry));
            return true;
        }
        if (wait_.kind == EWaitKind::WaitUntil)
        {
            if (ExecuteAssert(step, true))
            {
                return true;
            }
            const uint32_t timeoutMs = step.value("timeoutMs", defaultStepTimeoutMs_);
            const double elapsedMs = (engine_.GetTime() - wait_.startTime) * 1000.0;
            if (elapsedMs >= static_cast<double>(timeoutMs))
            {
                RecordFailure(step, fmt::format("wait-until timed out after {} ms", timeoutMs), step.value("fatal", false));
                return true;
            }
            return false;
        }
        return true;
    }

    bool FAgentDriver::ExecuteStep(const json& step)
    {
        const std::string type = step.value("type", "");
        if (type == "key" || type == "text" || type == "mouse-move" || type == "mouse-button" || type == "click" ||
            type == "drag" || type == "scroll")
        {
            return ExecuteInputStep(step, type);
        }
        if (type == "wait-frames")
        {
            const uint32_t frames = step.value("n", defaultWaitFrames_);
            wait_ = {.kind = EWaitKind::Frames, .startFrame = engine_.GetTotalFrames(),
                     .targetFrame = engine_.GetTotalFrames() + frames};
            return true;
        }
        if (type == "wait-ms")
        {
            const uint32_t ms = step.value("ms", 0u);
            wait_ = {.kind = EWaitKind::Milliseconds, .startTime = engine_.GetTime(),
                     .targetTime = engine_.GetTime() + static_cast<double>(ms) / 1000.0};
            return true;
        }
        if (type == "wait-until")
        {
            wait_ = {.kind = EWaitKind::WaitUntil, .startFrame = engine_.GetTotalFrames(),
                     .startTime = engine_.GetTime()};
            return false;
        }
        if (type == "screenshot")
        {
            return ExecuteScreenshot(step);
        }
        if (type == "assert")
        {
            ExecuteAssert(step, false);
            return true;
        }
        if (type == "cvar")
        {
            return ExecuteCVar(step);
        }
        if (type == "exec")
        {
            return ExecuteExec(step);
        }
        if (type == "log")
        {
            RecordStep({{"type", "log"}, {"message", step.value("message", step.value("text", ""))}, {"passed", true}});
            return true;
        }
        if (type == "quit")
        {
            Finish(failed_ ? 1 : 0);
            return false;
        }

        RecordFailure(step, fmt::format("unknown step type '{}'", type), true);
        return false;
    }

    bool FAgentDriver::ExecuteInputStep(const json& step, const std::string& type)
    {
        SDL_Window* window = engine_.GetWindow().Handle();
        const SDL_WindowID windowId = SDL_GetWindowID(window);
        const auto current = Runtime::Input::Synthetic::FPoint{
            static_cast<float>(engine_.GetMousePos().x),
            static_cast<float>(engine_.GetMousePos().y),
        };

        if (type == "key")
        {
            const std::string code = step.value("code", "");
            const auto key = Runtime::Input::Synthetic::ResolveKeyCode(code);
            const auto scan = Runtime::Input::Synthetic::ResolveScanCode(key, code);
            std::vector<std::string> mods;
            if (step.contains("mods") && step["mods"].is_array())
            {
                mods = step["mods"].get<std::vector<std::string>>();
            }
            const auto mod = Runtime::Input::Synthetic::ResolveModifiers(mods);
            const std::string action = step.value("action", "press");
            if (key == SDLK_UNKNOWN)
            {
                RecordFailure(step, fmt::format("unknown key '{}'", code), true);
                return false;
            }
            if (action == "down")
            {
                Runtime::Input::Synthetic::PushKey(windowId, key, scan, mod, true);
                SetKeyActive(code, true);
            }
            else if (action == "up")
            {
                Runtime::Input::Synthetic::PushKey(windowId, key, scan, mod, false);
                SetKeyActive(code, false);
            }
            else
            {
                Runtime::Input::Synthetic::PushKeyPress(windowId, key, scan, mod);
            }
            SetKeyboardToast(fmt::format("KEY {} {}", code, action));
            RecordStep({{"type", type}, {"code", code}, {"action", action}, {"passed", true}});
            return true;
        }

        if (type == "text")
        {
            const std::string text = step.value("value", "");
            Runtime::Input::Synthetic::PushText(windowId, text);
            SetKeyboardToast(fmt::format("TEXT {}", text));
            RecordStep({{"type", type}, {"passed", true}});
            return true;
        }

        if (type == "mouse-move")
        {
            if (step.value("relative", false))
            {
                const auto to = ResolvePoint(step.value("to", json::array({0.0, 0.0})));
                engine_.InjectRelativeMouse(to.x, to.y);
                const auto pos = Runtime::Input::Synthetic::FPoint{
                    static_cast<float>(engine_.GetMousePos().x),
                    static_cast<float>(engine_.GetMousePos().y),
                };
                SetMouseMarker(pos, to);
            }
            else
            {
                const auto to = ResolvePoint(step.at("to"));
                Runtime::Input::Synthetic::PushMouseMove(window, current, to);
                SetMouseMarker(to, {to.x - current.x, to.y - current.y});
            }
            RecordStep({{"type", type}, {"passed", true}});
            return true;
        }

        if (type == "click")
        {
            const auto at = step.contains("at") ? ResolvePoint(step["at"]) : current;
            if (step.contains("at"))
            {
                Runtime::Input::Synthetic::PushMouseMove(window, current, at);
            }
            const Uint8 button = Runtime::Input::Synthetic::ResolveMouseButton(step.value("button", "left"));
            const Uint8 clicks = static_cast<Uint8>(std::max(1, step.value("count", 1)));
            Runtime::Input::Synthetic::PushMouseButton(windowId, at, button, true, clicks);
            Runtime::Input::Synthetic::PushMouseButton(windowId, at, button, false, clicks);
            SetMouseMarker(at, {at.x - current.x, at.y - current.y});
            RecordStep({{"type", type}, {"passed", true}});
            return true;
        }

        if (type == "mouse-button")
        {
            const auto at = step.contains("at") ? ResolvePoint(step["at"]) : current;
            if (step.contains("at"))
            {
                Runtime::Input::Synthetic::PushMouseMove(window, current, at);
            }
            const Uint8 button = Runtime::Input::Synthetic::ResolveMouseButton(step.value("button", "left"));
            const Uint8 clicks = static_cast<Uint8>(std::max(1, step.value("count", 1)));
            const std::string action = step.value("action", "press");
            if (action == "down")
            {
                Runtime::Input::Synthetic::PushMouseButton(windowId, at, button, true, clicks);
                SetMouseButtonActive(step.value("button", "left"), true);
            }
            else if (action == "up")
            {
                Runtime::Input::Synthetic::PushMouseButton(windowId, at, button, false, clicks);
                SetMouseButtonActive(step.value("button", "left"), false);
            }
            else
            {
                Runtime::Input::Synthetic::PushMouseButton(windowId, at, button, true, clicks);
                Runtime::Input::Synthetic::PushMouseButton(windowId, at, button, false, clicks);
            }
            SetMouseMarker(at, {at.x - current.x, at.y - current.y});
            RecordStep({{"type", type}, {"button", step.value("button", "left")},
                        {"action", action}, {"passed", true}});
            return true;
        }

        if (type == "drag")
        {
            const auto from = ResolvePoint(step.at("from"));
            const auto to = ResolvePoint(step.at("to"));
            const Uint8 button = Runtime::Input::Synthetic::ResolveMouseButton(step.value("button", "left"));
            Runtime::Input::Synthetic::PushMouseMove(window, current, from);
            Runtime::Input::Synthetic::PushMouseButton(windowId, from, button, true);
            Runtime::Input::Synthetic::PushMouseMove(window, from, to);
            Runtime::Input::Synthetic::PushMouseButton(windowId, to, button, false);
            SetMouseMarker(to, {to.x - from.x, to.y - from.y});
            SetDragTrail(from, to);
            RecordStep({{"type", type}, {"passed", true}});
            return true;
        }

        if (type == "scroll")
        {
            const float x = step.value("x", 0.0f);
            const float y = step.value("y", 0.0f);
            Runtime::Input::Synthetic::PushMouseWheel(windowId, current, x, y);
            SetMouseMarker(current, {0.0f, 0.0f});
            RecordStep({{"type", type}, {"passed", true}});
            return true;
        }

        return true;
    }

    bool FAgentDriver::ExecuteScreenshot(const json& step)
    {
        const std::string fallback = fmt::format("screenshots/{}_step_{:03}", name_, stepIndex_);
        lastScreenshotPath_ = ResolveOutputPath(step.value("out", ""), fallback);
        Utilities::FileHelper::EnsureDirectoryExists(std::filesystem::path(lastScreenshotPath_).parent_path());
        engine_.RequestScreenShot({.filename = lastScreenshotPath_, .includeUi = step.value("ui", engine_.GetOptions().AgentValidationUI)});
        wait_ = {.kind = EWaitKind::Screenshot, .startFrame = engine_.GetTotalFrames()};
        return true;
    }

    bool FAgentDriver::ExecuteAssert(const json& step, bool waitUntil)
    {
        const std::string query = step.value("query", "");
        const std::string op = step.value("op", "eq");
        const json expected = step.contains("value") ? step["value"] : json();
        const auto actual = Query(query);
        const bool passed = actual.has_value() && Compare(*actual, op, expected);
        if (waitUntil && !passed)
        {
            return false;
        }

        json entry = {
            {"type", waitUntil ? "wait-until" : "assert"},
            {"query", query},
            {"op", op},
            {"expected", expected},
            {"passed", passed},
        };
        if (actual)
        {
            entry["actual"] = QueryValueToJson(*actual);
        }
        else
        {
            entry["message"] = "query not found";
        }
        RecordStep(entry);

        if (!passed)
        {
            failed_ = true;
            if (step.value("fatal", false))
            {
                Finish(1);
            }
        }
        return passed;
    }

    bool FAgentDriver::ExecuteCVar(const json& step)
    {
        const std::string name = step.value("name", "");
        if (step.contains("set"))
        {
            const std::string value = step["set"].is_string() ? step["set"].get<std::string>() : step["set"].dump();
            std::string error;
            const bool ok = engine_.GetCVarSystem().SetValueFromString(name, value, NextCVar::ECVarSetBy::Console, &error);
            RecordStep({{"type", "cvar"}, {"name", name}, {"value", value}, {"passed", ok}, {"message", error}});
            if (!ok)
            {
                failed_ = true;
            }
            return true;
        }

        bool found = false;
        const std::string value = engine_.GetCVarSystem().GetValueString(name, &found);
        RecordStep({{"type", "cvar"}, {"name", name}, {"value", value}, {"passed", found}});
        if (!found)
        {
            failed_ = true;
        }
        return true;
    }

    bool FAgentDriver::ExecuteExec(const json& step)
    {
        const std::string line = step.value("line", "");
        auto result = engine_.GetCVarSystem().ExecuteCommand(line);
        json entry = {
            {"type", "exec"},
            {"line", line},
            {"passed", result.success},
            {"message", result.message},
            {"output", result.output},
        };
        RecordStep(std::move(entry));
        if (!result.success)
        {
            failed_ = true;
        }
        return true;
    }

    std::optional<FAgentQueryValue> FAgentDriver::Query(const std::string& query) const
    {
        if (query == "engine.totalFrames")
        {
            return static_cast<int64_t>(engine_.GetTotalFrames());
        }
        if (query == "engine.frameRate")
        {
            return static_cast<double>(engine_.GetFrameRate());
        }
        if (query == "engine.time")
        {
            return engine_.GetTime();
        }
        if (query == "engine.status")
        {
            return StatusToString(engine_.GetEngineStatus());
        }
        if (query == "scene.nodeCount")
        {
            return static_cast<int64_t>(engine_.GetScene().Nodes().size());
        }
        if (query == "scene.selectedId")
        {
            return static_cast<int64_t>(engine_.GetScene().GetSelectedId());
        }
        if (query == "scene.selectedCount")
        {
            return static_cast<int64_t>(engine_.GetScene().GetSelectedIds().size());
        }
        constexpr std::string_view cvarPrefix = "cvar.";
        if (query.rfind(cvarPrefix.data(), 0) == 0)
        {
            bool found = false;
            std::string value = engine_.GetCVarSystem().GetValueString(query.substr(cvarPrefix.size()), &found);
            if (found)
            {
                return value;
            }
            return std::nullopt;
        }
        constexpr std::string_view gamePrefix = "game.";
        if (query.rfind(gamePrefix.data(), 0) == 0)
        {
            return gameQueries_.Query(query.substr(gamePrefix.size()));
        }
        return std::nullopt;
    }

    std::string FAgentDriver::ResolveOutputPath(const std::string& path, const std::string& fallbackStem) const
    {
        std::string chosen = path.empty() ? fallbackStem : path;
        if (IsAbsolutePath(chosen))
        {
            return chosen;
        }
        return Utilities::FileHelper::GetPlatformFilePath(chosen.c_str());
    }

    Runtime::Input::Synthetic::FPoint FAgentDriver::ResolvePoint(const json& value) const
    {
        int width = 0;
        int height = 0;
        SDL_GetWindowSize(engine_.GetWindow().Handle(), &width, &height);
        if (width <= 0)
        {
            width = static_cast<int>(engine_.GetOptions().Width);
        }
        if (height <= 0)
        {
            height = static_cast<int>(engine_.GetOptions().Height);
        }

        if (value.is_array() && value.size() >= 2)
        {
            return {value[0].get<float>(), value[1].get<float>()};
        }
        if (value.is_object())
        {
            if (value.contains("norm") && value["norm"].is_array() && value["norm"].size() >= 2)
            {
                return {value["norm"][0].get<float>() * static_cast<float>(width),
                        value["norm"][1].get<float>() * static_cast<float>(height)};
            }
            if (value.contains("px") && value["px"].is_array() && value["px"].size() >= 2)
            {
                return {value["px"][0].get<float>(), value["px"][1].get<float>()};
            }
            if (value.contains("x") && value.contains("y"))
            {
                return {value["x"].get<float>(), value["y"].get<float>()};
            }
        }
        return {0.0f, 0.0f};
    }

    bool FAgentDriver::Compare(const FAgentQueryValue& actual, const std::string& op, const json& expected) const
    {
        if (op == "not-empty")
        {
            return !ToString(actual).empty();
        }
        if (op == "empty")
        {
            return ToString(actual).empty();
        }
        if (op == "contains")
        {
            return expected.is_string() && ToString(actual).find(expected.get<std::string>()) != std::string::npos;
        }

        if (op == "eq" || op == "ne")
        {
            bool equal = false;
            if (expected.is_boolean() && std::holds_alternative<bool>(actual))
            {
                equal = expected.get<bool>() == std::get<bool>(actual);
            }
            else if (auto actualNumber = ToNumber(actual); actualNumber && ExpectedNumber(expected))
            {
                equal = std::abs(*actualNumber - *ExpectedNumber(expected)) < 0.0001;
            }
            else
            {
                const std::string expectedText = expected.is_string() ? expected.get<std::string>() : expected.dump();
                equal = ToString(actual) == expectedText;
            }
            return op == "eq" ? equal : !equal;
        }

        const auto actualNumber = ToNumber(actual);
        const auto expectedNumber = ExpectedNumber(expected);
        if (!actualNumber || !expectedNumber)
        {
            return false;
        }
        if (op == "gt")
        {
            return *actualNumber > *expectedNumber;
        }
        if (op == "ge")
        {
            return *actualNumber >= *expectedNumber;
        }
        if (op == "lt")
        {
            return *actualNumber < *expectedNumber;
        }
        if (op == "le")
        {
            return *actualNumber <= *expectedNumber;
        }
        return false;
    }

    void FAgentDriver::SetCurrentStepLabel(const json& step)
    {
        const std::string type = step.value("type", "<unknown>");
        if (step.contains("comment") && step["comment"].is_string())
        {
            currentStepLabel_ = fmt::format("{} - {}", type, step["comment"].get<std::string>());
            return;
        }
        if (type == "key")
        {
            currentStepLabel_ = fmt::format("key {} {}", step.value("code", ""), step.value("action", "press"));
        }
        else if (type == "mouse-button")
        {
            currentStepLabel_ = fmt::format("mouse {} {}", step.value("button", "left"), step.value("action", "press"));
        }
        else if (type == "wait-ms")
        {
            currentStepLabel_ = fmt::format("wait {} ms", step.value("ms", 0));
        }
        else if (type == "wait-frames")
        {
            currentStepLabel_ = fmt::format("wait {} frames", step.value("n", 0));
        }
        else if (type == "assert" || type == "wait-until")
        {
            currentStepLabel_ = fmt::format("{} {}", type, step.value("query", ""));
        }
        else
        {
            currentStepLabel_ = type;
        }
    }

    void FAgentDriver::SetKeyboardToast(std::string label)
    {
        keyboardToast_ = std::move(label);
        lastKeyboardActionTime_ = engine_.GetTime();
    }

    void FAgentDriver::SetMouseMarker(Runtime::Input::Synthetic::FPoint pos, Runtime::Input::Synthetic::FPoint delta)
    {
        lastMousePos_ = pos;
        lastMouseDelta_ = delta;
        lastMouseActionTime_ = engine_.GetTime();
    }

    void FAgentDriver::SetDragTrail(Runtime::Input::Synthetic::FPoint from, Runtime::Input::Synthetic::FPoint to)
    {
        dragStart_ = from;
        dragEnd_ = to;
        lastDragActionTime_ = engine_.GetTime();
    }

    void FAgentDriver::SetKeyActive(const std::string& code, bool active)
    {
        auto it = std::find(activeKeys_.begin(), activeKeys_.end(), code);
        if (active && it == activeKeys_.end())
        {
            activeKeys_.push_back(code);
        }
        else if (!active && it != activeKeys_.end())
        {
            activeKeys_.erase(it);
        }
    }

    void FAgentDriver::SetMouseButtonActive(const std::string& button, bool active)
    {
        auto it = std::find(activeMouseButtons_.begin(), activeMouseButtons_.end(), button);
        if (active && it == activeMouseButtons_.end())
        {
            activeMouseButtons_.push_back(button);
        }
        else if (!active && it != activeMouseButtons_.end())
        {
            activeMouseButtons_.erase(it);
        }
    }
}
