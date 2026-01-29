#include "Editor/EditorUi.hpp"

#include "Assets/Scene.hpp"
#include "Runtime/Engine.hpp"
#include "ThirdParty/fontawesome/IconsFontAwesome6.h"
#include "Utilities/ImGui.hpp"
#include "Utilities/Math.hpp"

namespace Editor
{
    namespace
    {
        constexpr float kToolIconWidth = 32.0f;
    }

    void DrawViewportOverlay(EditorContext& ctx, EditorUiState& ui)
    {
        if (!ui.viewportOnMainViewport)
        {
            return;
        }

        ImGuiViewport* viewport = ImGui::GetMainViewport();
        const ImVec2 pos = ui.viewportContentPos;
        const ImVec2 size = ui.viewportContentSize;
        if (size.x <= 0.0f || size.y <= 0.0f)
        {
            return;
        }

        constexpr float padding = 5.0f;
        const float statW = std::max(60.0f, std::min(160.0f, size.x - padding * 2.0f));
        const float statH = std::max(60.0f, std::min(140.0f, size.y - padding * 2.0f));

        ImGui::SetNextWindowPos(pos + ImVec2(padding, padding));
        ImGui::SetNextWindowSize(ImVec2(statW, statH));
        ImGui::SetNextWindowViewport(viewport->ID);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.5f));

        ImGuiWindowFlags windowFlags = 0 | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoSavedSettings;

        ImGui::Begin("ViewportStat", nullptr, windowFlags);

        const double smoothDelta = ctx.engine.GetSmoothDeltaSeconds();
        const double frameRate = smoothDelta > 0.0 ? (1.0 / smoothDelta) : 0.0;
        ImGui::Text("Realtime Statistics:");
        ImGui::Text("Frame rate: %.0f fps", frameRate);
        ImGui::Text("Progressive: %d", ctx.engine.IsProgressiveRendering());

        auto& gpuDrivenStat = ctx.scene.GetGpuDrivenStat();
        const uint32_t instanceCount = gpuDrivenStat.ProcessedCount - gpuDrivenStat.CulledCount;
        const uint32_t triangleCount = gpuDrivenStat.TriangleCount - gpuDrivenStat.CulledTriangleCount;
        ImGui::Text("Tris: %s/%s", Utilities::metricFormatter(static_cast<double>(triangleCount), "").c_str(),
                    Utilities::metricFormatter(static_cast<double>(gpuDrivenStat.TriangleCount), "").c_str());
        ImGui::Text("Draw: %s/%s", Utilities::metricFormatter(static_cast<double>(instanceCount), "").c_str(),
                    Utilities::metricFormatter(static_cast<double>(gpuDrivenStat.ProcessedCount), "").c_str());

        ImGui::End();

        const float toolH = kToolIconWidth + 8.0f;
        float toolW = kToolIconWidth + 16.0f;
        toolW = std::max(60.0f, std::min(toolW, size.x - padding * 2.0f));

        ImGui::SetNextWindowPos(pos + ImVec2(std::max(padding, size.x - toolW - padding), padding));
        ImGui::SetNextWindowSize(ImVec2(toolW, toolH));
        ImGui::SetNextWindowViewport(viewport->ID);
        ImGui::SetNextWindowBgAlpha(0);

        ImGui::Begin("ViewportTool", nullptr, windowFlags);

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.5f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);

        const float startX = ImGui::GetCursorPosX();
        const float availW = ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorPosX(startX + std::max(0.0f, availW - kToolIconWidth));
        if (ImGui::Button(ICON_FA_EYE, ImVec2(kToolIconWidth, kToolIconWidth)))
        {
            ImGui::OpenPopup("ViewportShowFlags");
        }
        BUTTON_TOOLTIP("Show Flags")

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 12));
        if (ImGui::BeginPopup("ViewportShowFlags"))
        {
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 10));
            auto& showFlags = ctx.engine.GetShowFlags();
            Utilities::UI::DrawShowFlagsCommon(showFlags);

            ImGui::PopStyleVar();
            ImGui::EndPopup();
        }
        ImGui::PopStyleVar();

        ImGui::PopStyleColor();
        ImGui::PopStyleVar();

        ImGui::PopStyleColor();
        ImGui::PopStyleVar();

        ImGui::End();
    }
} // namespace Editor
