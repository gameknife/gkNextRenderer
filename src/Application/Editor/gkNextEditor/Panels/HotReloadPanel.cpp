#include "EditorUi.hpp"

#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Editor/UI/DesktopUI.hpp"
#include "Modules/LiveCoding/LiveCodingModule.hpp"
#include "ThirdParty/fontawesome/IconsFontAwesome6.h"

#include <imgui.h>

namespace Editor
{
    namespace
    {
        void DrawPathRow(const char* label, const std::filesystem::path& path)
        {
            ImGui::TextUnformatted(label);
            ImGui::SameLine(150.0f);
            const std::string text = path.empty() ? std::string("(none)") : path.string();
            ImGui::TextWrapped("%s", text.c_str());
        }
    }

    void DrawHotReloadPanel(EditorContext& ctx, EditorUiState& ui)
    {
        if (!ImGui::Begin("Hot Reload", &ui.hotReloadPanel))
        {
            ImGui::End();
            return;
        }

        auto& options = ctx.engine.GetOptions();
        const auto status = ctx.engine.GetHotReloadStatus();

        bool shaderHotReload = options.ShaderHotReload;
        if (ImGui::Checkbox("Shader hot reload", &shaderHotReload))
        {
            options.ShaderHotReload = shaderHotReload;
        }

        float shaderInterval = options.ShaderHotReloadInterval;
        if (ImGui::SliderFloat("Shader poll interval", &shaderInterval, 0.1f, 5.0f, "%.1fs"))
        {
            options.ShaderHotReloadInterval = shaderInterval;
        }

        if (NextUI::Theme::IconButton(
                ICON_FA_WAND_MAGIC_SPARKLES "##RebuildShaders",
                "Rebuild shaders now"))
        {
            ctx.engine.RequestShaderHotReload();
        }

        ImGui::SameLine();
        const bool cppLiveCodingAvailable = Modules::LiveCoding::IsCppLiveCodingAvailable();
        ImGui::BeginDisabled(!cppLiveCodingAvailable);
        if (NextUI::Theme::IconButton(
                ICON_FA_HAMMER "##CompileCppLiveCoding",
                cppLiveCodingAvailable
                    ? "Compile C++ changes with Live++"
                    : "Live++ is not available in this process"))
        {
            Modules::LiveCoding::RequestCppReload();
        }
        ImGui::EndDisabled();

        ImGui::Separator();
        ImGui::Text("Shader: %s", status.shaderInitialized ? "ready" : "not initialized");
        DrawPathRow("Shader source", status.shaderSourceRoot);
        DrawPathRow("Shader output", status.shaderOutputRoot);
        DrawPathRow("slangc", status.shaderCompiler);

        ImGui::End();
    }
} // namespace Editor
