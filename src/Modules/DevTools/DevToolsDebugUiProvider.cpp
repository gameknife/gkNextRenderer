#include "Engine/Common/CoreMinimal.hpp"
#include "Modules/DevTools/DevToolsDebugUiProvider.hpp"
#include "Modules/DevTools/GraphicsDebugPanel.hpp"
#include "Modules/DevTools/PhysicsDebugOverlay.hpp"
#include "Modules/DevTools/ProfileDebugOverlay.hpp"
#include "Engine/Runtime/Editor/UI/DesktopUI.hpp"
#include "Modules/DevTools/ConsoleLogBuffer.hpp"
#include "Modules/DevTools/UiDevPanels.hpp"
#include "Modules/DevTools/CVarEditorPanel.hpp"
#include "Modules/DevTools/AuxDrawPass.hpp"
#include "Modules/DevTools/AuxDrawSystem.hpp"
#include "Modules/DevTools/UI/UiCatalog.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Config/CVarSystem.hpp"
#include "Engine/Rendering/ExternalPassRegistry.hpp"

namespace DevTools
{
    namespace
    {
        bool uiCatalogOpen = false;
    }

    void Install(NextEngine& engine)
    {
        engine.SetDebugDraw(std::make_shared<FAuxDrawSystem>());
        engine.SetDebugUiProvider(&DefaultDebugUiProvider());
        FUiDevPanels::Get().RegisterCVars(engine.GetCVarSystem());
        NextCVar::FCVarInfo existing;
        if (!engine.GetCVarSystem().TryGetInfo("ui.catalog", existing))
        {
            engine.GetCVarSystem().RegisterBool(
                "ui.catalog", false, &uiCatalogOpen, NextCVar::ECVarFlags::None,
                "Open the developer UI foundation catalog");
        }
    }

    namespace
    {
        class FDebugUiProvider final : public Runtime::IDebugUiProvider
        {
        public:
            FDebugUiProvider()
            {
                // Capture logs into the console buffer from the moment the
                // provider exists (the entry point creates it before the engine).
                Runtime::Editor::AttachConsoleLogSinkToDefaultLogger();
            }

            void DrawUiPanels(NextEngine& engine, const NextUI::Statistics& statistics,
                              NextUI::EUiDeveloperLayer layers, bool suppressStatsOverlay) override
            {
                FUiDevPanels& panels = FUiDevPanels::Get();
                if (NextUI::HasUiLayer(layers, NextUI::EUiDeveloperLayer::Statistics) && !suppressStatsOverlay)
                {
                    panels.DrawOverlay(statistics);
                }
                if (NextUI::HasUiLayer(layers, NextUI::EUiDeveloperLayer::Console))
                {
                    panels.RenderConsoleOverlay();
                }
                if (NextUI::HasUiLayer(layers, NextUI::EUiDeveloperLayer::Memory))
                {
                    panels.DrawMemoryStatisticsPanel(engine);
                }
                Runtime::DevToolsUI::DrawUiCatalog(uiCatalogOpen);
            }

            bool HandleUiEvent(const SDL_Event& event) override
            {
                return FUiDevPanels::Get().HandleEvent(event);
            }

            void DrawPhysicsOverlay(const Assets::Scene& scene, const Assets::Camera& camera) override
            {
                Runtime::DrawPhysicsDebugOverlay(scene, camera);
            }

            void DrawGraphicsPanel(NextEngine& engine, bool& panelVisible, float topOffset) override
            {
                Runtime::GraphicsDebugPanel::DrawPanel(engine, panelVisible, topOffset);
            }

            void DrawCVarEditor(NextEngine& engine, bool& panelVisible) override
            {
                DrawCVarEditorPanel(engine, panelVisible);
            }

            void DrawProfileOverlay(NextEngine& engine, const NextUI::Statistics& statistics,
                                    float topOffset) override
            {
                Runtime::DrawProfileDebugOverlay(engine, statistics, topOffset);
            }

            bool HandleRendererShortcut(SDL_Keycode key, bool pressed, bool panelVisible, NextEngine& engine) override
            {
                return Runtime::GraphicsDebugPanel::TryHandleRendererShortcut(key, pressed, panelVisible, engine);
            }

            bool HandleViewModeShortcut(SDL_Keycode key, bool pressed, bool panelVisible,
                                        Runtime::Config::ShowFlags& showFlags) override
            {
                return Runtime::GraphicsDebugPanel::TryHandleViewModeShortcut(key, pressed, panelVisible, showFlags);
            }
        };
    }

    Runtime::IDebugUiProvider& DefaultDebugUiProvider()
    {
        static FDebugUiProvider provider;
        static bool auxDrawRegistered = false;
        if (!auxDrawRegistered)
        {
            // GPU aux drawing (debug lines/points) renders as an external overlay pass.
            Vulkan::RegisterExternalPassFactory(
                /*priority*/ 20,
                [](Vulkan::VulkanBaseRenderer& renderer) -> std::unique_ptr<Vulkan::IExternalRenderPass>
                { return std::make_unique<Vulkan::AuxDraw::AuxDrawPass>(renderer); });
            auxDrawRegistered = true;
        }
        return provider;
    }
}
