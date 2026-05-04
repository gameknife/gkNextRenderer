#pragma once

#include "Common/CoreMinimal.hpp"

#include <imgui.h>

namespace NextUI::Painter
{
    void DrawImageContain(ImDrawList* drawList,
                          ImTextureID texture,
                          const ImVec2& textureSize,
                          const ImVec2& boxMin,
                          const ImVec2& boxMax,
                          float padding = 0.0f,
                          ImU32 tint = IM_COL32_WHITE);
    void DrawImageCover(ImDrawList* drawList,
                        ImTextureID texture,
                        const ImVec2& textureSize,
                        const ImVec2& boxMin,
                        const ImVec2& boxMax,
                        ImU32 tint = IM_COL32_WHITE);
    void DrawNineSlicePanel(ImDrawList* drawList,
                            ImTextureID texture,
                            const ImVec2& textureSize,
                            const ImVec2& min,
                            const ImVec2& max,
                            ImU32 tint = IM_COL32_WHITE);
    void DrawPanel(ImDrawList* drawList,
                   ImTextureID texture,
                   const ImVec2& min,
                   const ImVec2& max,
                   ImU32 fallbackColor,
                   float rounding,
                   ImU32 tint = IM_COL32_WHITE,
                   const ImVec2& textureSize = ImVec2(64.0f, 64.0f));
    void DrawBar(const ImVec2& pos, const ImVec2& size, float ratio, ImU32 fillColor, const char* text, float scale);
    void DrawTexturedBar(ImDrawList* drawList,
                         ImTextureID bgTexture,
                         ImTextureID fillTexture,
                         ImTextureID frameTexture,
                         const ImVec2& pos,
                         const ImVec2& size,
                         float ratio,
                         const std::string& text,
                         float scale,
                         ImU32 fillFallback,
                         ImU32 textColor = IM_COL32_WHITE);
    void DrawFullscreenDim(const ImGuiViewport* viewport, float alpha);
    bool BeginAppModal(const char* title, float dimAlpha = 0.55f, ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize);
    void EndAppModal();
}
