#include "EditorUi.hpp"

#include "Engine/Runtime/Config/CVarSystem.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Modules/DevTools/ProfessionalUI.hpp"
#include "Modules/LiveCoding/LiveCodingModule.hpp"
#include "ThirdParty/fontawesome/IconsFontAwesome6.h"

#include <fmt/format.h>
#include <imgui.h>
#include <spdlog/spdlog.h>

namespace Editor
{
    namespace
    {
        void SetCVarBool(NextCVar::FCVarSystem& cvars, const char* name, bool value)
        {
            std::string error;
            cvars.SetValueFromString(name, value ? "1" : "0", NextCVar::ECVarSetBy::Console, &error);
            if (!error.empty())
            {
                SPDLOG_WARN("{}", error);
            }
        }

        void SetCVarFloat(NextCVar::FCVarSystem& cvars, const char* name, float value)
        {
            std::string error;
            cvars.SetValueFromString(name, fmt::format("{:.3f}", value), NextCVar::ECVarSetBy::Console, &error);
            if (!error.empty())
            {
                SPDLOG_WARN("{}", error);
            }
        }

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
        auto& cvars = ctx.engine.GetCVarSystem();
        const auto status = ctx.engine.GetHotReloadStatus();

        bool shaderHotReload = options.ShaderHotReload;
        if (ImGui::Checkbox("Shader hot reload", &shaderHotReload))
        {
            SetCVarBool(cvars, "r.shader.hot_reload", shaderHotReload);
        }

        float shaderInterval = options.ShaderHotReloadInterval;
        if (ImGui::SliderFloat("Shader poll interval", &shaderInterval, 0.1f, 5.0f, "%.1fs"))
        {
            SetCVarFloat(cvars, "r.shader.hot_reload_interval", shaderInterval);
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
