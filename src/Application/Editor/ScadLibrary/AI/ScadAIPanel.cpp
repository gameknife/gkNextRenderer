#include "Engine/Common/CoreMinimal.hpp"
#include "ScadAIPanel.hpp"

#include "Modules/DevTools/ProfessionalUI.hpp"

#include <imgui.h>

namespace ScadLibrary::AI
{
    namespace
    {
        const FScadAIProviderOption* FindProvider(const FScadAITransportConfiguration& configuration,
                                                  const std::string& providerId)
        {
            const auto found = std::find_if(
                configuration.providers.begin(), configuration.providers.end(),
                [&providerId](const FScadAIProviderOption& provider) { return provider.id == providerId; });
            return found == configuration.providers.end() ? nullptr : &*found;
        }
    }

    void FScadAIPanel::RefreshConfiguration(FScadAIController& controller)
    {
        configurationRequested_ = true;
        configurationLoaded_ = controller.LoadTransportConfiguration(configuration_, configurationError_);
    }

    void FScadAIPanel::Draw(const std::string& targetLabel, FScadAIController& controller, bool canApply,
                            bool canUndo, bool showingCandidate, const FScadAIPanelActions& actions)
    {
        const FScadAIControllerSnapshot snapshot = controller.Snapshot();
        const bool generating = snapshot.state == EScadAIProposalState::Generating ||
            snapshot.state == EScadAIProposalState::Validating;
        if (!configurationRequested_ && !generating)
        {
            RefreshConfiguration(controller);
        }

        ImGui::Spacing();
        ImGui::TextDisabled("目标");
        ImGui::SameLine();
        ImGui::TextWrapped("%s", targetLabel.c_str());
        ImGui::TextDisabled("Profile: scad-authoring · stateless · 本地严格校验");

        const FScadAIProviderOption* selectedProvider =
            FindProvider(configuration_, configuration_.currentProviderId);
        const char* providerPreview = selectedProvider
            ? selectedProvider->displayName.c_str()
            : (configuration_.currentProviderId.empty() ? "未选择" : configuration_.currentProviderId.c_str());
        ImGui::TextDisabled("Provider");
        ImGui::BeginDisabled(generating || !configurationLoaded_);
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::BeginCombo("##scad_ai_provider", providerPreview))
        {
            for (const FScadAIProviderOption& provider : configuration_.providers)
            {
                const bool selected = provider.id == configuration_.currentProviderId;
                const bool enabled = provider.configured && provider.available;
                ImGui::BeginDisabled(!enabled);
                if (ImGui::Selectable(provider.displayName.c_str(), selected) && !selected)
                {
                    configurationLoaded_ =
                        controller.SelectProvider(provider.id, configuration_, configurationError_);
                }
                ImGui::EndDisabled();
                if (!enabled && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                {
                    ImGui::SetTooltip("%s", provider.configured ? "Provider 当前不可用" : "Provider 尚未配置");
                }
            }
            ImGui::EndCombo();
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("选择 AI Provider");
        }

        selectedProvider = FindProvider(configuration_, configuration_.currentProviderId);
        ImGui::TextDisabled("模型");
        ImGui::BeginDisabled(generating || !configurationLoaded_ || !selectedProvider ||
                             selectedProvider->models.empty());
        ImGui::SetNextItemWidth(-1.0f);
        const char* modelPreview =
            configuration_.currentModelId.empty() ? "默认模型" : configuration_.currentModelId.c_str();
        if (ImGui::BeginCombo("##scad_ai_model", modelPreview))
        {
            for (const std::string& model : selectedProvider->models)
            {
                const bool selected = model == configuration_.currentModelId;
                if (ImGui::Selectable(model.c_str(), selected) && !selected)
                {
                    configurationLoaded_ =
                        controller.SelectModel(model, configuration_, configurationError_);
                }
            }
            ImGui::EndCombo();
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("选择当前 Provider 的模型");
        }

        if (!configurationError_.empty())
        {
            ImGui::PushStyleColor(ImGuiCol_Text, NextUI::Theme::Color(NextUI::Theme::EColor::Warning));
            ImGui::TextWrapped("%s", configurationError_.c_str());
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::BeginDisabled(generating);
            if (ImGui::SmallButton("重试"))
            {
                RefreshConfiguration(controller);
            }
            ImGui::EndDisabled();
        }
        else if (!configuration_.statusMessage.empty())
        {
            ImGui::TextDisabled("%s", configuration_.statusMessage.c_str());
        }
        ImGui::Separator();

        ImGui::InputTextMultiline("##scad_ai_instruction", instruction_, sizeof(instruction_),
                                  ImVec2(-1.0f, 116.0f));
        ImGui::BeginDisabled(generating || instruction_[0] == '\0');
        if (ImGui::Button("生成提案", ImVec2(104.0f, 0.0f)) && actions.submit)
        {
            actions.submit(instruction_);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!generating);
        if (ImGui::Button("取消", ImVec2(72.0f, 0.0f)))
        {
            controller.Cancel();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled("%s", ProposalStateName(snapshot.state));

        if (!snapshot.statusMessage.empty())
        {
            ImGui::TextWrapped("%s", snapshot.statusMessage.c_str());
        }
        if (generating && !snapshot.streamText.empty())
        {
            ImGui::BeginChild("##scad_ai_stream", ImVec2(0.0f, 100.0f), ImGuiChildFlags_Borders);
            ImGui::TextWrapped("%s", snapshot.streamText.c_str());
            ImGui::EndChild();
        }
        if (!snapshot.proposal)
        {
            ImGui::Spacing();
            ImGui::TextDisabled("AI 只生成候选；预览、应用和保存都由你确认。");
            return;
        }

        const FScadAIProposal& proposal = *snapshot.proposal;
        ImGui::SeparatorText("提案");
        ImGui::TextWrapped("%s", proposal.summary.empty() ? "无摘要" : proposal.summary.c_str());
        if (proposal.repairCount > 0)
        {
            ImGui::TextDisabled("本地校验触发了 %d 次修复", proposal.repairCount);
        }
        for (const std::string& line : proposal.semanticDiff)
        {
            ImGui::BulletText("%s", line.c_str());
        }
        for (const FScadAIValidationIssue& issue : proposal.issues)
        {
            const ImVec4 color = issue.severity == EScadAIValidationSeverity::Error
                ? NextUI::Theme::Color(NextUI::Theme::EColor::Danger)
                : NextUI::Theme::Color(NextUI::Theme::EColor::Warning);
            ImGui::PushStyleColor(ImGuiCol_Text, color);
            ImGui::TextWrapped("[%s] %s", issue.code.c_str(), issue.message.c_str());
            ImGui::PopStyleColor();
        }

        const bool ready = proposal.state == EScadAIProposalState::Ready;
        ImGui::BeginDisabled(!ready || !canApply);
        if (ImGui::Button("预览候选") && actions.preview)
        {
            actions.preview();
        }
        ImGui::SameLine();
        if (ImGui::Button("对比原案") && actions.compareOriginal)
        {
            actions.compareOriginal();
        }
        ImGui::SameLine();
        if (ImGui::Button("应用到草稿") && actions.apply)
        {
            actions.apply();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!ready);
        if (ImGui::Button("拒绝"))
        {
            if (actions.reject)
            {
                actions.reject();
            }
            else
            {
                controller.Reject();
            }
        }
        ImGui::EndDisabled();
        if (ready)
        {
            ImGui::TextDisabled("Viewport 当前显示：%s", showingCandidate ? "AI 提案" : "原案");
        }
        if (proposal.state == EScadAIProposalState::Stale)
        {
            if (ImGui::Button("基于当前内容重新生成") && actions.regenerate)
            {
                actions.regenerate();
            }
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!canUndo);
        if (ImGui::Button("撤销上次 AI 修改") && actions.undo)
        {
            actions.undo();
        }
        ImGui::EndDisabled();
    }
} // namespace ScadLibrary::AI
