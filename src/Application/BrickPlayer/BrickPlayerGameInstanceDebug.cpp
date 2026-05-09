#include "BrickPlayerGameInstance.hpp"
#include "BrickPlayerSnapLogic.hpp"

#include "Assets/Core/Node.h"
#include "Assets/Loaders/FLDrawTypes.h"
#include "Runtime/Components/PhysicsComponent.h"
#include "Runtime/Subsystems/NextPhysics.h"

#include <imgui.h>

namespace
{
    const char* BoolLabel(bool value)
    {
        return value ? "yes" : "no";
    }

    const char* SnapKindLabel(BrickPlayer::Shadow::ESnapKind kind)
    {
        switch (kind)
        {
        case BrickPlayer::Shadow::ESnapKind::Cylinder:
            return "Cylinder";
        default:
            return "Unknown";
        }
    }

    const char* SnapGenderLabel(BrickPlayer::Shadow::ESnapGender gender)
    {
        switch (gender)
        {
        case BrickPlayer::Shadow::ESnapGender::Male:
            return "Male";
        case BrickPlayer::Shadow::ESnapGender::Female:
            return "Female";
        case BrickPlayer::Shadow::ESnapGender::Unknown:
        default:
            return "Unknown";
        }
    }

    std::string GetPartDisplayName(const std::string& partFile)
    {
        if (partFile.empty())
        {
            return "<unknown>";
        }

        std::string displayName = std::filesystem::path(partFile).filename().string();
        if (displayName.empty())
        {
            displayName = partFile;
        }
        return displayName;
    }

    std::string DescribeConnectorProfiles(const BrickPlayer::Shadow::FSnapConnector& connector)
    {
        if (connector.sections.empty())
        {
            return "none";
        }

        std::string result;
        for (const BrickPlayer::Shadow::FCylinderSection& section : connector.sections)
        {
            if (!result.empty())
            {
                result += ", ";
            }
            result += section.profile.empty() ? "?" : section.profile;
        }

        return result;
    }

    std::string DescribeConnector(const BrickPlayer::Shadow::FSnapConnector& connector, int32_t connectorIndex)
    {
        return fmt::format("#{} | {} | {} | group={} | id={} | r={:.4f} | l={:.4f} | slide={} | centered={} | profiles={}",
                           connectorIndex,
                           SnapKindLabel(connector.kind),
                           SnapGenderLabel(connector.gender),
                           connector.group.empty() ? "-" : connector.group,
                           connector.id.empty() ? "-" : connector.id,
                           connector.radius,
                           connector.length,
                           BoolLabel(connector.slide),
                           BoolLabel(connector.centered),
                           DescribeConnectorProfiles(connector));
    }
}

void BrickPlayerGameInstance::DrawSnapConfirmation() const
{
    if (!sceneLoaded_ || !isDraggingPart_ || !activeSnapCandidate_.valid)
    {
        return;
    }

    const bool pulsing = GetEngine().GetTime() < snapFeedbackPulseUntil_;
    const glm::vec4 snapColor = pulsing
        ? glm::vec4(0.35f, 1.0f, 0.35f, 1.0f)
        : glm::vec4(0.15f, 0.95f, 0.25f, 0.95f);
    const float pointSize = pulsing ? 8.0f : 5.5f;
    const float lineSize = pulsing ? 3.0f : 2.0f;

    NextEngineHelper::DrawAuxPoint(activeSnapCandidate_.desiredTranslation, snapColor, pointSize);

    if (activeSnapCandidate_.restoreOriginalHierarchy
        || activeSnapCandidate_.draggedConnectorIndex < 0
        || activeSnapCandidate_.targetConnectorIndex < 0)
    {
        return;
    }

    const std::vector<WorldSnapConnector> draggedConnectors = BuildWorldConnectors(draggedInstanceId_);
    const std::vector<WorldSnapConnector> targetConnectors = BuildWorldConnectors(activeSnapCandidate_.targetInstanceId);
    if (static_cast<size_t>(activeSnapCandidate_.draggedConnectorIndex) >= draggedConnectors.size()
        || static_cast<size_t>(activeSnapCandidate_.targetConnectorIndex) >= targetConnectors.size())
    {
        return;
    }

    const WorldSnapConnector& draggedConnector = draggedConnectors[activeSnapCandidate_.draggedConnectorIndex];
    const WorldSnapConnector& targetConnector = targetConnectors[activeSnapCandidate_.targetConnectorIndex];
    if (!draggedConnector.connector || !targetConnector.connector)
    {
        return;
    }

    const float draggedAxisLength = std::max(std::max(draggedConnector.connector->length, draggedConnector.connector->radius * 2.0f), 0.01f);
    const float targetAxisLength = std::max(std::max(targetConnector.connector->length, targetConnector.connector->radius * 2.0f), 0.01f);

    NextEngineHelper::DrawAuxPoint(draggedConnector.worldPosition, snapColor, pointSize + 1.0f);
    NextEngineHelper::DrawAuxPoint(targetConnector.worldPosition, glm::vec4(1.0f, 0.95f, 0.35f, 1.0f), pointSize + 1.0f);
    NextEngineHelper::DrawAuxLine(draggedConnector.worldPosition, targetConnector.worldPosition, snapColor, lineSize);
    NextEngineHelper::DrawAuxLine(draggedConnector.worldPosition,
                                  draggedConnector.worldPosition + draggedConnector.worldAxis * draggedAxisLength,
                                  snapColor,
                                  lineSize);
    NextEngineHelper::DrawAuxLine(targetConnector.worldPosition,
                                  targetConnector.worldPosition + targetConnector.worldAxis * targetAxisLength,
                                  glm::vec4(1.0f, 0.95f, 0.35f, 1.0f),
                                  lineSize);
}

void BrickPlayerGameInstance::DrawSnapDebug() const
{
    if (!showSnapDebug_ || !sceneLoaded_)
    {
        return;
    }

    const bool hasDraggedPart = isDraggingPart_ && draggedInstanceId_ != UINT32_MAX;
    const std::vector<WorldSnapConnector> draggedConnectors = hasDraggedPart
        ? BuildWorldConnectors(draggedInstanceId_)
        : std::vector<WorldSnapConnector>{};
    const BrickPlayer::Shadow::FSnapConnector* lockedDraggedConnector = hasDraggedPart
        ? GetLockedDraggedConnector(draggedInstanceId_)
        : nullptr;
    const bool hasLockedConnector = lockedDraggedConnector
        && lockedDraggedConnectorIndex_ >= 0
        && static_cast<size_t>(lockedDraggedConnectorIndex_) < draggedConnectors.size();

    auto getPartFile = [&](uint32_t instanceId) -> std::string
    {
        auto partIt = nodePartFileMap_.find(instanceId);
        if (partIt == nodePartFileMap_.end())
        {
            return {};
        }

        return partIt->second;
    };

    const float lduScale = GetLduToWorldScale();
    auto isTargetCompatible = [&](const BrickPlayer::Shadow::FSnapConnector& target) -> bool
    {
        if (hasLockedConnector)
            return BrickPlayer::Snap::AreConnectorsCompatible(*lockedDraggedConnector, target, lduScale);
        for (const WorldSnapConnector& dc : draggedConnectors)
        {
            if (dc.connector && BrickPlayer::Snap::AreConnectorsCompatible(*dc.connector, target, lduScale))
                return true;
        }
        return false;
    };

    auto countCompatibleTargetConnectors = [&](const std::vector<WorldSnapConnector>& candidateConnectors) -> int
    {
        if (!hasDraggedPart || draggedConnectors.empty())
            return 0;
        int count = 0;
        for (const WorldSnapConnector& tc : candidateConnectors)
        {
            if (tc.connector && isTargetCompatible(*tc.connector))
                count++;
        }
        return count;
    };

    auto drawConnector = [](const WorldSnapConnector& connector, const glm::vec4& color, float pointSize, float lineSize)
    {
        if (!connector.connector)
        {
            return;
        }

        const float axisLength = std::max(std::max(connector.connector->length, connector.connector->radius * 2.0f), 0.01f);
        NextEngineHelper::DrawAuxPoint(connector.worldPosition, color, pointSize);
        NextEngineHelper::DrawAuxLine(connector.worldPosition,
                                      connector.worldPosition + connector.worldAxis * axisLength,
                                      color,
                                      lineSize);
    };

    if (hasDraggedPart && !draggedConnectors.empty())
    {
        for (size_t connectorIndex = 0; connectorIndex < draggedConnectors.size(); ++connectorIndex)
        {
            glm::vec4 color(0.2f, 0.75f, 1.0f, 1.0f);
            float pointSize = 4.0f;
            float lineSize = 1.5f;

            if (static_cast<int32_t>(connectorIndex) == lockedDraggedConnectorIndex_)
            {
                color = glm::vec4(0.1f, 1.0f, 1.0f, 1.0f);
                pointSize = 6.0f;
                lineSize = 2.0f;
            }
            if (activeSnapCandidate_.valid && static_cast<int32_t>(connectorIndex) == activeSnapCandidate_.draggedConnectorIndex)
            {
                color = glm::vec4(0.1f, 1.0f, 0.2f, 1.0f);
                pointSize = 7.0f;
                lineSize = 2.5f;
            }

            drawConnector(draggedConnectors[connectorIndex], color, pointSize, lineSize);
        }
    }

    std::vector<WorldSnapConnector> hoveredTargetConnectors;
    int hoverFilteredCompatibleCount = 0;
    if (hasDraggedPart && hoveredAssembly_.instanceId != UINT32_MAX)
    {
        hoveredTargetConnectors = BuildWorldConnectors(hoveredAssembly_.instanceId);

        if (!hoveredTargetConnectors.empty())
        {
            NextEngineHelper::DrawAuxPoint(hoveredAssembly_.hitPoint, glm::vec4(1.0f, 0.2f, 1.0f, 1.0f), 5.0f);

            for (size_t targetConnectorIndex = 0; targetConnectorIndex < hoveredTargetConnectors.size(); ++targetConnectorIndex)
            {
                const WorldSnapConnector& targetConnector = hoveredTargetConnectors[targetConnectorIndex];
                if (!targetConnector.connector)
                {
                    continue;
                }

                const bool isCompatible = isTargetCompatible(*targetConnector.connector);

                const BrickPlayer::Snap::FHoverFilterResult hoverFilter =
                    BrickPlayer::Snap::EvaluateHoverFilter(*targetConnector.connector,
                                                           targetConnector.worldPosition,
                                                           targetConnector.worldAxis,
                                                           hoveredAssembly_.hitPoint,
                                                           hoveredAssembly_.hitNormal,
                                                           lduScale);
                const bool passesHoverFilter = hoverFilter.passes;

                if (isCompatible && passesHoverFilter)
                {
                    hoverFilteredCompatibleCount++;
                }

                glm::vec4 color = isCompatible ? glm::vec4(1.0f, 0.85f, 0.2f, 1.0f) : glm::vec4(1.0f, 0.3f, 0.3f, 0.65f);
                float pointSize = passesHoverFilter ? 5.0f : 3.5f;
                float lineSize = passesHoverFilter ? 2.0f : 1.0f;
                if (activeSnapCandidate_.valid && static_cast<int32_t>(targetConnectorIndex) == activeSnapCandidate_.targetConnectorIndex)
                {
                    color = glm::vec4(0.1f, 1.0f, 0.2f, 1.0f);
                    pointSize = 7.0f;
                    lineSize = 2.5f;
                }

                drawConnector(targetConnector, color, pointSize, lineSize);

                if (hasLockedConnector && isCompatible && passesHoverFilter)
                {
                    const WorldSnapConnector& lockedWorldConnector = draggedConnectors[lockedDraggedConnectorIndex_];
                    const bool isActivePair = activeSnapCandidate_.valid
                        && activeSnapCandidate_.draggedConnectorIndex == lockedDraggedConnectorIndex_
                        && activeSnapCandidate_.targetConnectorIndex == static_cast<int32_t>(targetConnectorIndex);
                    NextEngineHelper::DrawAuxLine(lockedWorldConnector.worldPosition,
                                                  targetConnector.worldPosition,
                                                  isActivePair ? glm::vec4(0.1f, 1.0f, 0.2f, 1.0f)
                                                               : glm::vec4(1.0f, 0.85f, 0.2f, 0.8f),
                                                  isActivePair ? 2.5f : 1.0f);
                }
            }
        }
    }

    const uint32_t debugTargetInstanceId = activeSnapCandidate_.valid ? activeSnapCandidate_.targetInstanceId : hoveredAssembly_.instanceId;
    const std::vector<WorldSnapConnector> debugTargetConnectors = debugTargetInstanceId != UINT32_MAX
        ? BuildWorldConnectors(debugTargetInstanceId)
        : std::vector<WorldSnapConnector>{};
    const int compatibleTargetCount = countCompatibleTargetConnectors(debugTargetConnectors);

    const ImVec4 sectionColor(1.0f, 0.84f, 0.45f, 1.0f);
    const ImVec4 labelColor(0.62f, 0.66f, 0.72f, 1.0f);
    const ImVec4 valueColor(0.94f, 0.95f, 0.97f, 1.0f);
    const ImVec4 accentColor(0.55f, 0.82f, 1.0f, 1.0f);
    const ImVec4 successColor(0.52f, 0.95f, 0.52f, 1.0f);
    const ImVec4 warningColor(1.0f, 0.82f, 0.42f, 1.0f);
    const ImVec4 subtleColor(0.55f, 0.58f, 0.62f, 1.0f);
    const ImVec4 dangerColor(1.0f, 0.52f, 0.52f, 1.0f);

    auto drawSectionTitle = [&](const char* title)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, sectionColor);
        ImGui::SeparatorText(title);
        ImGui::PopStyleColor();
    };

    auto drawKeyValue = [&](const char* label, const std::string& value, const ImVec4& currentValueColor)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, labelColor);
        ImGui::TextUnformatted(label);
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, currentValueColor);
        ImGui::TextUnformatted(value.c_str());
        ImGui::PopStyleColor();
    };

    auto drawStandaloneValue = [&](const std::string& value, const ImVec4& currentValueColor)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, currentValueColor);
        ImGui::TextUnformatted(value.c_str());
        ImGui::PopStyleColor();
    };

    auto boolColor = [&](bool value) -> ImVec4
    {
        return value ? successColor : subtleColor;
    };

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + viewport->Size.x - 20.0f, viewport->Pos.y + 56.0f),
                            ImGuiCond_Always,
                            ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.85f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 12.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 5.0f));

    if (ImGui::Begin("Snap Debug",
                     nullptr,
                     ImGuiWindowFlags_NoCollapse
                         | ImGuiWindowFlags_NoMove
                         | ImGuiWindowFlags_NoResize
                         | ImGuiWindowFlags_NoSavedSettings
                         | ImGuiWindowFlags_AlwaysAutoResize))
    {
        if (!hasDraggedPart)
        {
            drawKeyValue("State:", "idle", subtleColor);
            drawStandaloneValue("Drag a disassembled part to inspect its text info, part name and connector state.",
                                subtleColor);
        }
        else
        {
            auto* draggedNode = GetEngine().GetScene().GetNodeByInstanceId(draggedInstanceId_);
            const std::string draggedPartFile = getPartFile(draggedInstanceId_);
            const std::string draggedPartName = GetPartDisplayName(draggedPartFile);

            std::string snapState = "Free drag";
            if (activeSnapCandidate_.valid)
            {
                if (activeSnapCandidate_.restoreOriginalHierarchy)
                {
                    snapState = "Candidate: restore original hierarchy";
                }
                else
                {
                    snapState = fmt::format("Candidate: connector #{} -> target #{}",
                                            activeSnapCandidate_.draggedConnectorIndex,
                                            activeSnapCandidate_.targetConnectorIndex);
                }
            }
            else if (hoveredAssembly_.instanceId != UINT32_MAX)
            {
                snapState = "Hovering assembled part, no valid candidate";
            }

            const ImVec4 stateColor = activeSnapCandidate_.valid
                ? (activeSnapCandidate_.restoreOriginalHierarchy ? warningColor : successColor)
                : (hoveredAssembly_.instanceId != UINT32_MAX ? warningColor : accentColor);

            drawKeyValue("State:", snapState, stateColor);
            drawKeyValue("Hover target:", hoveredAssembly_.instanceId != UINT32_MAX ? "yes" : "no",
                         boolColor(hoveredAssembly_.instanceId != UINT32_MAX));

            drawSectionTitle("Dragged Part");
            drawKeyValue("Node name:", draggedNode ? draggedNode->GetName() : "<missing>",
                         draggedNode ? valueColor : dangerColor);
            drawKeyValue("Part text:", draggedPartFile.empty() ? "<unknown>" : draggedPartFile,
                         draggedPartFile.empty() ? dangerColor : accentColor);
            drawKeyValue("Part name:", draggedPartName, accentColor);
            drawKeyValue("Instance:", fmt::format("{}", draggedInstanceId_), valueColor);
            drawKeyValue("Connector count:", fmt::format("{}", static_cast<int>(draggedConnectors.size())),
                         draggedConnectors.empty() ? subtleColor : valueColor);
            drawKeyValue("Locked connector:", hasLockedConnector ? "yes" : "no", boolColor(hasLockedConnector));

            if (hasLockedConnector)
            {
                const std::string lockedConnectorText = DescribeConnector(*lockedDraggedConnector, lockedDraggedConnectorIndex_);
                drawKeyValue("Locked detail:", lockedConnectorText, successColor);
            }
            else if (draggedConnectors.empty())
            {
                drawStandaloneValue("This part has no shadow connectors.", subtleColor);
            }

            if (activeSnapCandidate_.valid
                && !activeSnapCandidate_.restoreOriginalHierarchy
                && activeSnapCandidate_.draggedConnectorIndex >= 0
                && static_cast<size_t>(activeSnapCandidate_.draggedConnectorIndex) < draggedConnectors.size()
                && draggedConnectors[activeSnapCandidate_.draggedConnectorIndex].connector)
            {
                const std::string activeDraggedConnectorText =
                    DescribeConnector(*draggedConnectors[activeSnapCandidate_.draggedConnectorIndex].connector,
                                      activeSnapCandidate_.draggedConnectorIndex);
                drawKeyValue("Active dragged connector:", activeDraggedConnectorText, successColor);
            }

            drawSectionTitle("Connector Status");
            drawKeyValue("Compatible target connectors:",
                         fmt::format("{}/{}", compatibleTargetCount, static_cast<int>(debugTargetConnectors.size())),
                         compatibleTargetCount > 0 ? successColor : subtleColor);
            drawKeyValue("Hover-filtered matches:",
                         fmt::format("{}", hoverFilteredCompatibleCount),
                         hoverFilteredCompatibleCount > 0 ? successColor : subtleColor);
            drawKeyValue("Restore original hierarchy:",
                         BoolLabel(activeSnapCandidate_.valid && activeSnapCandidate_.restoreOriginalHierarchy),
                         boolColor(activeSnapCandidate_.valid && activeSnapCandidate_.restoreOriginalHierarchy));

            drawSectionTitle("Target Part");
            if (debugTargetInstanceId == UINT32_MAX)
            {
                drawStandaloneValue("No assembled part under cursor.", subtleColor);
            }
            else
            {
                auto* targetNode = GetEngine().GetScene().GetNodeByInstanceId(debugTargetInstanceId);
                const std::string targetPartFile = getPartFile(debugTargetInstanceId);
                const std::string targetPartName = GetPartDisplayName(targetPartFile);

                drawKeyValue("Node name:", targetNode ? targetNode->GetName() : "<missing>",
                             targetNode ? valueColor : dangerColor);
                drawKeyValue("Part text:", targetPartFile.empty() ? "<unknown>" : targetPartFile,
                             targetPartFile.empty() ? dangerColor : accentColor);
                drawKeyValue("Part name:", targetPartName, accentColor);
                drawKeyValue("Instance:", fmt::format("{}", debugTargetInstanceId), valueColor);
                drawKeyValue("Connector count:", fmt::format("{}", static_cast<int>(debugTargetConnectors.size())),
                             debugTargetConnectors.empty() ? subtleColor : valueColor);
                drawKeyValue("Hovered target:",
                             debugTargetInstanceId == hoveredAssembly_.instanceId ? "yes" : "no",
                             boolColor(debugTargetInstanceId == hoveredAssembly_.instanceId));

                if (hoveredAssembly_.instanceId != UINT32_MAX)
                {
                    drawKeyValue("Hover point:",
                                 fmt::format("{:.4f}, {:.4f}, {:.4f}",
                                             hoveredAssembly_.hitPoint.x,
                                             hoveredAssembly_.hitPoint.y,
                                             hoveredAssembly_.hitPoint.z),
                                 valueColor);
                }

                if (activeSnapCandidate_.valid
                    && !activeSnapCandidate_.restoreOriginalHierarchy
                    && activeSnapCandidate_.targetConnectorIndex >= 0
                    && static_cast<size_t>(activeSnapCandidate_.targetConnectorIndex) < debugTargetConnectors.size()
                    && debugTargetConnectors[activeSnapCandidate_.targetConnectorIndex].connector)
                {
                    const std::string activeTargetConnectorText =
                        DescribeConnector(*debugTargetConnectors[activeSnapCandidate_.targetConnectorIndex].connector,
                                          activeSnapCandidate_.targetConnectorIndex);
                    drawKeyValue("Active target connector:", activeTargetConnectorText, successColor);
                }
            }
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
}
