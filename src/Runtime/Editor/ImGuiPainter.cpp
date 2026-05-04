#include "Runtime/Editor/ImGuiPainter.h"

namespace NextUI::Painter
{
    void DrawImageContain(ImDrawList* drawList,
                          ImTextureID texture,
                          const ImVec2& textureSize,
                          const ImVec2& boxMin,
                          const ImVec2& boxMax,
                          float padding,
                          ImU32 tint)
    {
        if (!drawList || !texture)
        {
            return;
        }

        const float availWidth = std::max(1.0f, (boxMax.x - boxMin.x) - padding * 2.0f);
        const float availHeight = std::max(1.0f, (boxMax.y - boxMin.y) - padding * 2.0f);
        if (textureSize.x <= 0.0f || textureSize.y <= 0.0f)
        {
            drawList->AddImage(texture,
                               ImVec2(boxMin.x + padding, boxMin.y + padding),
                               ImVec2(boxMin.x + padding + availWidth, boxMin.y + padding + availHeight),
                               ImVec2(0.0f, 0.0f),
                               ImVec2(1.0f, 1.0f),
                               tint);
            return;
        }

        const float scale = std::min(availWidth / textureSize.x, availHeight / textureSize.y);
        const ImVec2 drawSize(textureSize.x * scale, textureSize.y * scale);
        const ImVec2 drawMin(boxMin.x + padding + (availWidth - drawSize.x) * 0.5f,
                             boxMin.y + padding + (availHeight - drawSize.y) * 0.5f);
        drawList->AddImage(texture,
                           drawMin,
                           ImVec2(drawMin.x + drawSize.x, drawMin.y + drawSize.y),
                           ImVec2(0.0f, 0.0f),
                           ImVec2(1.0f, 1.0f),
                           tint);
    }

    void DrawImageCover(ImDrawList* drawList,
                        ImTextureID texture,
                        const ImVec2& textureSize,
                        const ImVec2& boxMin,
                        const ImVec2& boxMax,
                        ImU32 tint)
    {
        if (!drawList || !texture)
        {
            return;
        }

        if (textureSize.x <= 0.0f || textureSize.y <= 0.0f)
        {
            drawList->AddImage(texture, boxMin, boxMax, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), tint);
            return;
        }

        const float boxWidth = std::max(1.0f, boxMax.x - boxMin.x);
        const float boxHeight = std::max(1.0f, boxMax.y - boxMin.y);
        const float boxAspect = boxWidth / boxHeight;
        const float imageAspect = textureSize.x / textureSize.y;
        ImVec2 uvMin(0.0f, 0.0f);
        ImVec2 uvMax(1.0f, 1.0f);

        if (imageAspect > boxAspect)
        {
            const float visibleWidth = boxAspect / imageAspect;
            uvMin.x = (1.0f - visibleWidth) * 0.5f;
            uvMax.x = uvMin.x + visibleWidth;
        }
        else
        {
            const float visibleHeight = imageAspect / boxAspect;
            uvMin.y = (1.0f - visibleHeight) * 0.5f;
            uvMax.y = uvMin.y + visibleHeight;
        }

        drawList->AddImage(texture, boxMin, boxMax, uvMin, uvMax, tint);
    }

    void DrawNineSlicePanel(ImDrawList* drawList,
                            ImTextureID texture,
                            const ImVec2& textureSize,
                            const ImVec2& min,
                            const ImVec2& max,
                            ImU32 tint)
    {
        if (!drawList || !texture)
        {
            return;
        }

        if (textureSize.x < 3.0f || textureSize.y < 3.0f)
        {
            drawList->AddImage(texture, min, max, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), tint);
            return;
        }

        const float width = std::max(1.0f, max.x - min.x);
        const float height = std::max(1.0f, max.y - min.y);
        const float leftPixels = std::floor(textureSize.x * 0.5f);
        const float rightPixels = std::max(0.0f, textureSize.x - leftPixels - 1.0f);
        const float topPixels = std::floor(textureSize.y * 0.5f);
        const float bottomPixels = std::max(0.0f, textureSize.y - topPixels - 1.0f);
        const float leftWidth = std::min(leftPixels, width * 0.5f);
        const float rightWidth = std::min(rightPixels, width * 0.5f);
        const float topHeight = std::min(topPixels, height * 0.5f);
        const float bottomHeight = std::min(bottomPixels, height * 0.5f);
        const float x0 = min.x;
        const float x1 = min.x + leftWidth;
        const float x2 = max.x - rightWidth;
        const float x3 = max.x;
        const float y0 = min.y;
        const float y1 = min.y + topHeight;
        const float y2 = max.y - bottomHeight;
        const float y3 = max.y;
        const float u0 = 0.0f;
        const float u1 = leftWidth / textureSize.x;
        const float u2 = 1.0f - rightWidth / textureSize.x;
        const float u3 = 1.0f;
        const float v0 = 0.0f;
        const float v1 = topHeight / textureSize.y;
        const float v2 = 1.0f - bottomHeight / textureSize.y;
        const float v3 = 1.0f;

        auto addPatch = [drawList, texture, tint](float ax, float ay, float bx, float by, float au, float av, float bu, float bv)
        {
            drawList->AddImage(texture, ImVec2(ax, ay), ImVec2(bx, by), ImVec2(au, av), ImVec2(bu, bv), tint);
        };

        addPatch(x0, y0, x1, y1, u0, v0, u1, v1);
        addPatch(x1, y0, x2, y1, u1, v0, u2, v1);
        addPatch(x2, y0, x3, y1, u2, v0, u3, v1);
        addPatch(x0, y1, x1, y2, u0, v1, u1, v2);
        addPatch(x1, y1, x2, y2, u1, v1, u2, v2);
        addPatch(x2, y1, x3, y2, u2, v1, u3, v2);
        addPatch(x0, y2, x1, y3, u0, v2, u1, v3);
        addPatch(x1, y2, x2, y3, u1, v2, u2, v3);
        addPatch(x2, y2, x3, y3, u2, v2, u3, v3);
    }

    void DrawPanel(ImDrawList* drawList,
                   ImTextureID texture,
                   const ImVec2& min,
                   const ImVec2& max,
                   ImU32 fallbackColor,
                   float rounding,
                   ImU32 tint,
                   const ImVec2& textureSize)
    {
        if (!drawList)
        {
            return;
        }

        if (texture)
        {
            DrawNineSlicePanel(drawList, texture, textureSize, min, max, tint);
            return;
        }

        drawList->AddRectFilled(min, max, fallbackColor, rounding);
    }

    void DrawBar(const ImVec2& pos, const ImVec2& size, float ratio, ImU32 fillColor, const char* text, float scale)
    {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        if (!drawList)
        {
            return;
        }

        drawList->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), IM_COL32(28, 30, 34, 230), 3.0f * scale);
        drawList->AddRectFilled(pos, ImVec2(pos.x + size.x * std::clamp(ratio, 0.0f, 1.0f), pos.y + size.y), fillColor,
                                3.0f * scale);
        drawList->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), IM_COL32(255, 255, 255, 90), 3.0f * scale);
        const ImVec2 textSize = ImGui::CalcTextSize(text);
        drawList->AddText(ImVec2(pos.x + (size.x - textSize.x) * 0.5f, pos.y + (size.y - textSize.y) * 0.5f), IM_COL32_WHITE,
                          text);
    }

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
                         ImU32 textColor)
    {
        if (!drawList)
        {
            return;
        }

        const ImVec2 max(pos.x + size.x, pos.y + size.y);
        const float clampedRatio = std::clamp(ratio, 0.0f, 1.0f);
        if (bgTexture)
        {
            drawList->AddImage(bgTexture, pos, max);
        }
        else
        {
            drawList->AddRectFilled(pos, max, IM_COL32(28, 30, 34, 230), 3.0f * scale);
        }

        const ImVec2 fillMax(pos.x + size.x * clampedRatio, pos.y + size.y);
        if (clampedRatio > 0.0f)
        {
            if (fillTexture)
            {
                drawList->AddImage(fillTexture, pos, fillMax, ImVec2(0.0f, 0.0f), ImVec2(clampedRatio, 1.0f));
            }
            else
            {
                drawList->AddRectFilled(pos, fillMax, fillFallback, 3.0f * scale);
            }
        }

        if (frameTexture)
        {
            drawList->AddImage(frameTexture, pos, max);
        }
        else
        {
            drawList->AddRect(pos, max, IM_COL32(255, 255, 255, 90), 3.0f * scale);
        }

        const ImVec2 textSize = ImGui::CalcTextSize(text.c_str());
        drawList->AddText(ImVec2(pos.x + (size.x - textSize.x) * 0.5f, pos.y + (size.y - textSize.y) * 0.5f),
                          textColor,
                          text.c_str());
    }

    void DrawFullscreenDim(const ImGuiViewport* viewport, float alpha)
    {
        if (!viewport)
        {
            return;
        }

        ImGui::GetBackgroundDrawList()->AddRectFilled(
            viewport->Pos,
            ImVec2(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y),
            ImGui::ColorConvertFloat4ToU32(ImVec4(0.0f, 0.0f, 0.0f, std::clamp(alpha, 0.0f, 1.0f))));
    }

    bool BeginAppModal(const char* title, float dimAlpha, ImGuiWindowFlags flags)
    {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        DrawFullscreenDim(viewport, dimAlpha);
        if (viewport)
        {
            ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        }
        ImGui::OpenPopup(title);
        return ImGui::BeginPopupModal(title, nullptr, flags);
    }

    void EndAppModal()
    {
        ImGui::EndPopup();
    }
}
