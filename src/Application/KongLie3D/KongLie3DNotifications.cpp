#include "KongLie3DNotifications.hpp"

#include "KongLie3DStyle.hpp"

namespace
{
    constexpr float ToastWidth = KongLie3D::ScaleUi(280.0f);
    constexpr float ToastHeight = KongLie3D::ScaleUi(44.0f);
    constexpr float ToastGap = KongLie3D::ScaleUi(8.0f);
    constexpr float ToastSlideDurationMs = 200.0f;
    constexpr float ToastFadeDurationMs = 300.0f;
    constexpr float ToastSlideOffset = KongLie3D::ScaleUi(60.0f);
    constexpr size_t MaxToastCount = 4;

    ImVec4 GetToastAccentColor(KongLie3D::ENotificationKind kind)
    {
        switch (kind)
        {
        case KongLie3D::ENotificationKind::Success:
            return KongLie3D::Style::Highlight;
        case KongLie3D::ENotificationKind::Warning:
            return ImVec4(0.95f, 0.60f, 0.20f, 1.0f);
        case KongLie3D::ENotificationKind::Critical:
            return KongLie3D::Style::Hostile;
        case KongLie3D::ENotificationKind::Info:
        default:
            return KongLie3D::Style::Accent;
        }
    }
}

namespace KongLie3D
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

    void FNotificationCenter::Render(float bottomInset, float rightInset) const
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

        ImFont* font = KongLie3D::KongLieFonts::Body ? KongLie3D::KongLieFonts::Body : ImGui::GetFont();
        const float fontSize = font ? font->FontSize : ImGui::GetFontSize();

        size_t stackIndex = 0;
        for (auto iter = toasts_.rbegin(); iter != toasts_.rend(); ++iter, ++stackIndex)
        {
            const FToast& toast = *iter;
            const float ageMs = toast.durationMs - toast.lifeMs;
            const float slideT = std::clamp(ageMs / ToastSlideDurationMs, 0.0f, 1.0f);
            const float fadeT = toast.lifeMs < ToastFadeDurationMs ? std::clamp(toast.lifeMs / ToastFadeDurationMs, 0.0f, 1.0f) : 1.0f;
            const float alpha = std::min(slideT, 1.0f) * fadeT;
            const float xOffset = (1.0f - slideT) * ToastSlideOffset;

            const float x = viewport->Pos.x + viewport->Size.x - ToastWidth - rightInset + xOffset;
            const float y = viewport->Pos.y + viewport->Size.y - bottomInset - ToastHeight - stackIndex * (ToastHeight + ToastGap);
            const ImVec2 cardMin(x, y);
            const ImVec2 cardMax(x + ToastWidth, y + ToastHeight);

            const ImVec4 accent = GetToastAccentColor(toast.kind);
            const ImU32 bgColor = ImGui::ColorConvertFloat4ToU32(ImVec4(0.05f, 0.08f, 0.12f, 0.92f * alpha));
            const ImU32 borderColor = ImGui::ColorConvertFloat4ToU32(ImVec4(accent.x, accent.y, accent.z, 0.35f * alpha));
            const ImU32 accentColor = ImGui::ColorConvertFloat4ToU32(ImVec4(accent.x, accent.y, accent.z, alpha));
            drawList->AddRectFilled(cardMin, cardMax, bgColor, KongLie3D::ScaleUi(8.0f));
            drawList->AddRect(cardMin, cardMax, borderColor, KongLie3D::ScaleUi(8.0f), 0, KongLie3D::ScaleUi(1.0f));
            drawList->AddRectFilled(cardMin,
                                    ImVec2(cardMin.x + KongLie3D::ScaleUi(6.0f), cardMax.y),
                                    accentColor,
                                    KongLie3D::ScaleUi(8.0f),
                                    ImDrawFlags_RoundCornersLeft);

            const ImVec2 textSize =
                font->CalcTextSizeA(fontSize, ToastWidth - KongLie3D::ScaleUi(28.0f), 0.0f, toast.text.c_str());
            const ImVec2 textPos(cardMin.x + KongLie3D::ScaleUi(18.0f), cardMin.y + (ToastHeight - textSize.y) * 0.5f);
            drawList->AddText(font,
                              fontSize,
                              textPos,
                              ImGui::ColorConvertFloat4ToU32(ImVec4(0.96f, 0.97f, 1.0f, alpha)),
                              toast.text.c_str());
        }
    }
}
