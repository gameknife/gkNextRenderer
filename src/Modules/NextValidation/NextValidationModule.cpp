#include "Modules/NextValidation/NextValidationModule.hpp"

#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Options.hpp"
#include "Engine/Rendering/VulkanBaseRenderer.hpp"
#include "Engine/Runtime/Config/CVarSystem.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Interface/AgentControl.hpp"
#include "Engine/Utilities/FileHelper.hpp"
#include "Modules/NextValidation/AgentControlServer.hpp"
#include "Modules/NextValidation/SyntheticInput.hpp"

#include <nlohmann/json.hpp>

namespace
{
    class FAgentControlService final : public Runtime::Agent::IAgentControlService
    {
    public:
        explicit FAgentControlService(NextEngine& engine) : engine_(engine)
        {
            const auto& options = engine_.GetOptions();
            std::string error;
            if (!server_.Start(options.AgentControl, options.AgentControlToken, error))
            {
                SPDLOG_ERROR("[AgentControl] failed to start: {}", error);
                engine_.RequestExit(3);
            }
            else
            {
                SPDLOG_INFO("[AgentControl] listening on {}", options.AgentControl);
            }
        }

        bool IsRunning() const override { return server_.IsRunning(); }

        void Pump() override
        {
            server_.Pump([this](const std::string& method, const nlohmann::json& params)
            {
                return HandleCommand(method, params);
            });
        }

        void Stop() override { server_.Stop(); }

    private:
        using FPoint = Runtime::Input::Synthetic::FPoint;

        static FPoint ReadPoint(const nlohmann::json& value)
        {
            if (!value.is_array() || value.size() < 2)
            {
                throw std::runtime_error("point must be [x,y]");
            }
            return {value[0].get<float>(), value[1].get<float>()};
        }

        nlohmann::json HandleCommand(const std::string& method, const nlohmann::json& params)
        {
            using namespace Runtime::Input::Synthetic;
            SDL_Window* window = engine_.GetWindow().Handle();
            const SDL_WindowID windowId = window != nullptr ? SDL_GetWindowID(window) : 0;
            const FPoint current{
                static_cast<float>(engine_.GetMousePos().x),
                static_cast<float>(engine_.GetMousePos().y)};

            if (method == "handshake")
            {
                return {{"protocolVersion", 1},
                        {"capabilities", {"input", "query", "cvar", "exec", "screenshot", "quit"}}};
            }
            if (method == "query")
            {
                const auto value = Query(params.value("query", ""));
                if (!value) throw std::runtime_error("query not found");
                return std::visit([](const auto& item) { return nlohmann::json(item); }, *value);
            }
            if (method == "key")
            {
                const std::string code = params.value("code", "");
                const auto key = ResolveKeyCode(code);
                if (key == SDLK_UNKNOWN) throw std::runtime_error("unknown key");
                const auto scan = ResolveScanCode(key, code);
                const auto modifiers = ResolveModifiers(params.value("mods", std::vector<std::string>{}));
                const std::string action = params.value("action", "press");
                if (action == "down") PushKey(windowId, key, scan, modifiers, true);
                else if (action == "up") PushKey(windowId, key, scan, modifiers, false);
                else PushKeyPress(windowId, key, scan, modifiers);
                return {{"ok", true}};
            }
            if (method == "text")
            {
                PushText(windowId, params.value("value", ""));
                return {{"ok", true}};
            }
            if (method == "mouse-move")
            {
                const FPoint to = ReadPoint(params.at("to"));
                if (params.value("relative", false)) engine_.InjectRelativeMouse(to.x, to.y);
                else PushMouseMove(window, current, to);
                return {{"ok", true}};
            }
            if (method == "mouse-button" || method == "click")
            {
                const FPoint at = params.contains("at") ? ReadPoint(params["at"]) : current;
                if (params.contains("at")) PushMouseMove(window, current, at);
                const auto button = ResolveMouseButton(params.value("button", "left"));
                const auto clicks = static_cast<Uint8>(std::max(1, params.value("count", 1)));
                const std::string action = method == "click" ? "press" : params.value("action", "press");
                if (action != "up") PushMouseButton(windowId, at, button, true, clicks);
                if (action != "down") PushMouseButton(windowId, at, button, false, clicks);
                return {{"ok", true}};
            }
            if (method == "drag")
            {
                const FPoint from = ReadPoint(params.at("from"));
                const FPoint to = ReadPoint(params.at("to"));
                const auto button = ResolveMouseButton(params.value("button", "left"));
                PushMouseMove(window, current, from);
                PushMouseButton(windowId, from, button, true);
                PushMouseMove(window, from, to);
                PushMouseButton(windowId, to, button, false);
                return {{"ok", true}};
            }
            if (method == "scroll")
            {
                PushMouseWheel(windowId, current, params.value("x", 0.0f), params.value("y", 0.0f));
                return {{"ok", true}};
            }
            if (method == "cvar")
            {
                const std::string name = params.value("name", "");
                if (params.contains("set"))
                {
                    std::string error;
                    const std::string value = params["set"].is_string()
                        ? params["set"].get<std::string>()
                        : params["set"].dump();
                    if (!engine_.GetCVarSystem().SetValueFromString(
                            name, value, NextCVar::ECVarSetBy::Console, &error))
                    {
                        throw std::runtime_error(error);
                    }
                    return {{"value", value}};
                }
                bool found = false;
                const auto value = engine_.GetCVarSystem().GetValueString(name, &found);
                if (!found) throw std::runtime_error("cvar not found");
                return {{"value", value}};
            }
            if (method == "exec")
            {
                const auto result = engine_.GetCVarSystem().ExecuteCommand(params.value("line", ""));
                if (!result.success) throw std::runtime_error(result.message);
                return {{"message", result.message}, {"output", result.output}};
            }
            if (method == "screenshot")
            {
                const std::string path = Utilities::FileHelper::GetWritableFilePath(
                    params.value("out", "screenshots/agent_validation").c_str());
                Utilities::FileHelper::EnsureDirectoryExists(
                    std::filesystem::path(path).parent_path().string());
                for (const char* extension : {".jpg", ".avif"})
                {
                    std::error_code errorCode;
                    std::filesystem::remove(path + extension, errorCode);
                    if (errorCode)
                    {
                        throw std::runtime_error(
                            "failed to remove existing screenshot " + path + extension + ": " +
                            errorCode.message());
                    }
                }
                const bool exitAfterCapture = params.value("quitAfterCapture", false);
                SPDLOG_INFO("[AgentControl] screenshot requested; exitAfterCapture={}", exitAfterCapture);
                engine_.RequestScreenShot({
                    .filename = path,
                    .accumulateFrames = params.value("accumulateFrames", 0u),
                    .sync = true,
                    .includeUi = params.value("ui", false),
                    .exitAfterCapture = exitAfterCapture,
                });
                return {{"path", path + ".jpg"}};
            }
            if (method == "quit")
            {
                const int exitCode = params.value("exitCode", 0);
                SPDLOG_INFO("[AgentControl] exit requested with code {}", exitCode);
                engine_.RequestExit(exitCode);
                return {{"ok", true}};
            }
            throw std::runtime_error("unknown agent control method: " + method);
        }

        std::optional<Runtime::Agent::FAgentQueryValue> Query(const std::string& query) const
        {
            if (query == "engine.totalFrames") return static_cast<int64_t>(engine_.GetTotalFrames());
            if (query == "engine.frameRate") return static_cast<double>(engine_.GetFrameRate());
            if (query == "engine.time") return engine_.GetTime();
            if (query == "engine.status")
            {
                switch (engine_.GetEngineStatus())
                {
                case NextRenderer::EApplicationStatus::Starting: return std::string("Starting");
                case NextRenderer::EApplicationStatus::Running: return std::string("Running");
                case NextRenderer::EApplicationStatus::Loading: return std::string("Loading");
                default: return std::string("AsyncPreparing");
                }
            }
            const auto& renderer = engine_.GetRenderer();
            const auto& scene = engine_.GetScene();
            if (query == "engine.rendererType") return static_cast<int64_t>(renderer.CurrentLogicRendererType());
            if (query == "engine.checkerboardActive") return renderer.IsCheckerboardRenderingActive();
            if (query == "engine.sparseCheckerboardActive") return renderer.IsSparseCheckerboardLightingActive();
            if (query == "scene.nodeCount") return static_cast<int64_t>(scene.Nodes().size());
            if (query == "scene.renderProxyCount") return static_cast<int64_t>(scene.GetIndirectDrawBatchCount());
            if (query == "scene.maxVisibleProxyIndex")
                return static_cast<int64_t>(scene.GetGpuDrivenStat().MaxVisibleProxyIndex);
            // Triangles the GPU-driven cull actually submitted last frame. This is what makes a LOD
            // change measurable on a machine whose frame rate is bound by something else entirely.
            if (query == "scene.drawnTriangleCount")
                return static_cast<int64_t>(scene.GetGpuDrivenStat().TriangleCount);
            if (query == "scene.drawnProxyCount")
                return static_cast<int64_t>(scene.GetGpuDrivenStat().ProcessedCount);
            // What the drawn proxies would have cost at LOD0; compare against drawnTriangleCount
            // to get what discrete LOD removed.
            if (query == "scene.lod0TriangleCount")
                return static_cast<int64_t>(scene.GetGpuDrivenStat().Lod0TriangleCount);
            // Occlusion-culled counts. Separate from the totals above because they come from the
            // previous frame's depth and can oscillate on their own, independently of anything the
            // frustum test does.
            if (query == "scene.culledTriangleCount")
                return static_cast<int64_t>(scene.GetGpuDrivenStat().CulledTriangleCount);
            if (query == "scene.culledProxyCount")
                return static_cast<int64_t>(scene.GetGpuDrivenStat().CulledCount);
            // Rasterised sun shadow geometry, summed over cascades. Since the cull pass selects a
            // LOD only after the cascade frustum test, TriangleCount covers exactly what is drawn.
            if (query == "scene.shadowVisibleTriangleCount")
            {
                int64_t total = 0;
                for (const auto& cascade : scene.GetShadowGpuDrivenStats())
                {
                    total += static_cast<int64_t>(cascade.TriangleCount);
                }
                return total;
            }
            // Triangles per drawn proxy, which is the LOD level the cull pass picked (80/40/20/10
            // for a scene of faceted asteroids) independent of where the camera is. Computed here
            // rather than by dividing two separate queries: the stats are refreshed every frame, so
            // two queries can land on different frames and their ratio is meaningless.
            if (query == "scene.drawnTrianglesPerProxy")
            {
                const auto& stat = scene.GetGpuDrivenStat();
                return stat.ProcessedCount > 0
                    ? static_cast<double>(stat.TriangleCount) / static_cast<double>(stat.ProcessedCount)
                    : 0.0;
            }
            if (query == "scene.sunElevation") return static_cast<double>(scene.GetEnvSettings().SunElevation);
            if (query == "scene.atmosphereEnabled") return scene.GetEnvSettings().AtmosphereEnabled;
            if (query == "scene.aerialPerspectiveEnabled") return scene.GetEnvSettings().AerialPerspectiveEnabled;
            if (query == "scene.heightFogEnabled") return scene.GetEnvSettings().HeightFogEnabled;
            if (query == "scene.selectedId") return static_cast<int64_t>(scene.GetSelectedId());
            if (query == "scene.selectedCount") return static_cast<int64_t>(scene.GetSelectedIds().size());
            if (query.rfind("cvar.", 0) == 0)
            {
                bool found = false;
                auto value = engine_.GetCVarSystem().GetValueString(query.substr(5), &found);
                return found ? std::optional<Runtime::Agent::FAgentQueryValue>(std::move(value)) : std::nullopt;
            }
            if (query.rfind("game.", 0) == 0) return engine_.GetAgentQueries().Query(query.substr(5));
            return std::nullopt;
        }

        NextEngine& engine_;
        Runtime::Agent::FAgentControlServer server_;
    };
}

namespace Modules::NextValidation
{
    void Install(NextEngine& engine)
    {
        if (!engine.GetOptions().AgentControl.empty())
        {
            engine.SetAgentControlService(std::make_unique<FAgentControlService>(engine));
        }
    }
}
