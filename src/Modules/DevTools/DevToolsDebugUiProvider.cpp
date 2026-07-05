#include "Engine/Common/CoreMinimal.hpp"
#include "Modules/DevTools/DevToolsDebugUiProvider.hpp"
#include "Modules/DevTools/GraphicsDebugPanel.hpp"
#include "Modules/DevTools/PhysicsDebugOverlay.hpp"
#include "Modules/DevTools/ProfileDebugOverlay.hpp"
#include "Modules/DevTools/ProfessionalUI.hpp"
#include "Modules/DevTools/ConsoleLogBuffer.hpp"
#include "Modules/DevTools/UiDevPanels.hpp"
#include "Modules/DevTools/CVarEditorPanel.hpp"

namespace DevTools
{
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

            void ApplyUiStyle() override
            {
                NextUI::Theme::ApplyProfessionalTheme();
            }

            void DrawUiPanels(NextEngine& engine, const NextUI::Statistics& statistics,
                              VulkanGpuTimer* gpuTimer, bool suppressStatsOverlay) override
            {
                FUiDevPanels& panels = FUiDevPanels::Get();
                if (!suppressStatsOverlay)
                {
                    panels.DrawOverlay(statistics, gpuTimer);
                }
                panels.RenderConsoleOverlay();
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
                                    VulkanGpuTimer* gpuTimer, float topOffset) override
            {
                Runtime::DrawProfileDebugOverlay(engine, statistics, gpuTimer, topOffset);
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
        return provider;
    }
}
