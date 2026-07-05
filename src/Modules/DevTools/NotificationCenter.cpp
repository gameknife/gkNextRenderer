#include "Modules/DevTools/NotificationCenter.h"

namespace
{
    constexpr float ToastSlideDurationMs = 200.0f;
    constexpr float ToastFadeDurationMs = 300.0f;
    constexpr size_t MaxToastCount = 4;
}

namespace NextUI
{
    void FNotificationCenter::Push(std::string text, ENotificationKind kind, float durationMs)
    {
        if (text.empty())
        {
            return;
        }

        toasts_.push_back(FToast{
            .text = std::move(text),
            .kind = kind,
            .lifeMs = durationMs,
            .durationMs = durationMs,
        });
        while (toasts_.size() > MaxToastCount)
        {
            toasts_.pop_front();
        }
    }

    void FNotificationCenter::Update(float deltaMs)
    {
        for (FToast& toast : toasts_)
        {
            toast.lifeMs = std::max(0.0f, toast.lifeMs - deltaMs);
        }

        while (!toasts_.empty() && toasts_.front().lifeMs <= 0.0f)
        {
            toasts_.pop_front();
        }
    }

    void FNotificationCenter::Clear()
    {
        toasts_.clear();
    }

    void FNotificationCenter::SetStyle(const FStyle& style)
    {
        style_ = style;
    }

    ImVec4 FNotificationCenter::GetAccentColor(ENotificationKind kind) const
    {
        switch (kind)
        {
        case ENotificationKind::Success:
            return style_.successAccent;
        case ENotificationKind::Warning:
            return style_.warningAccent;
        case ENotificationKind::Critical:
            return style_.criticalAccent;
        case ENotificationKind::Info:
        default:
            return style_.infoAccent;
        }
    }

    void FNotificationCenter::Render(float bottomInset, float rightInset, ImFont* font) const
    {
        if (toasts_.empty())
        {
            return;
        }

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImDrawList* drawList = ImGui::GetForegroundDrawList();
        if (!viewport || !drawList)
        {
            return;
        }

        font = font ? font : ImGui::GetFont();
        const float fontSize = font ? font->LegacySize : ImGui::GetFontSize();
        const float scale = std::max(0.01f, style_.uiScale);
        const float toastWidth = 280.0f * scale;
        const float toastHeight = 44.0f * scale;
        const float toastGap = 8.0f * scale;
        const float toastSlideOffset = 60.0f * scale;
        const float rounding = 8.0f * scale;

        size_t stackIndex = 0;
        for (auto iter = toasts_.rbegin(); iter != toasts_.rend(); ++iter, ++stackIndex)
        {
            const FToast& toast = *iter;
            const float ageMs = toast.durationMs - toast.lifeMs;
            const float slideT = std::clamp(ageMs / ToastSlideDurationMs, 0.0f, 1.0f);
            const float fadeT = toast.lifeMs < ToastFadeDurationMs ? std::clamp(toast.lifeMs / ToastFadeDurationMs, 0.0f, 1.0f) : 1.0f;
            const float alpha = slideT * fadeT;
            const float xOffset = (1.0f - slideT) * toastSlideOffset;

            const float x = viewport->Pos.x + viewport->Size.x - toastWidth - rightInset + xOffset;
            const float y = viewport->Pos.y + viewport->Size.y - bottomInset - toastHeight - stackIndex * (toastHeight + toastGap);
            const ImVec2 cardMin(x, y);
            const ImVec2 cardMax(x + toastWidth, y + toastHeight);

            const ImVec4 accent = GetAccentColor(toast.kind);
            const ImU32 bgColor = ImGui::ColorConvertFloat4ToU32(ImVec4(style_.surface.x, style_.surface.y, style_.surface.z,
                                                                         style_.surface.w * alpha));
            const ImU32 borderColor = ImGui::ColorConvertFloat4ToU32(ImVec4(accent.x, accent.y, accent.z, 0.35f * alpha));
            const ImU32 accentColor = ImGui::ColorConvertFloat4ToU32(ImVec4(accent.x, accent.y, accent.z, alpha));
            drawList->AddRectFilled(cardMin, cardMax, bgColor, rounding);
            drawList->AddRect(cardMin, cardMax, borderColor, rounding, 0, scale);
            drawList->AddRectFilled(cardMin,
                                    ImVec2(cardMin.x + 6.0f * scale, cardMax.y),
                                    accentColor,
                                    rounding,
                                    ImDrawFlags_RoundCornersLeft);

            const ImVec2 textSize = font->CalcTextSizeA(fontSize, toastWidth - 28.0f * scale, 0.0f, toast.text.c_str());
            const ImVec2 textPos(cardMin.x + 18.0f * scale, cardMin.y + (toastHeight - textSize.y) * 0.5f);
            drawList->AddText(font,
                              fontSize,
                              textPos,
                              ImGui::ColorConvertFloat4ToU32(ImVec4(style_.text.x, style_.text.y, style_.text.z, style_.text.w * alpha)),
                              toast.text.c_str());
        }
    }
}
