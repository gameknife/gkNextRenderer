#pragma once

#include "Common/CoreMinimal.hpp"

#include <deque>
#include <imgui.h>

namespace NextUI
{
    enum class ENotificationKind : uint8_t
    {
        Info,
        Success,
        Warning,
        Critical,
    };

    struct FToast
    {
        std::string text;
        ENotificationKind kind = ENotificationKind::Info;
        float lifeMs = 0.0f;
        float durationMs = 0.0f;
    };

    class FNotificationCenter
    {
    public:
        struct FStyle
        {
            ImVec4 infoAccent = ImVec4(0.38f, 0.68f, 1.0f, 1.0f);
            ImVec4 successAccent = ImVec4(0.45f, 0.95f, 0.52f, 1.0f);
            ImVec4 warningAccent = ImVec4(0.95f, 0.60f, 0.20f, 1.0f);
            ImVec4 criticalAccent = ImVec4(1.0f, 0.30f, 0.28f, 1.0f);
            ImVec4 surface = ImVec4(0.05f, 0.08f, 0.12f, 0.92f);
            ImVec4 text = ImVec4(0.96f, 0.97f, 1.0f, 1.0f);
            float uiScale = 1.0f;
        };

        void Push(std::string text, ENotificationKind kind, float durationMs = 2500.0f);
        void Update(float deltaMs);
        void Clear();
        void Render(float bottomInset = 0.0f, float rightInset = 12.0f, ImFont* font = nullptr) const;
        void SetStyle(const FStyle& style);

    private:
        ImVec4 GetAccentColor(ENotificationKind kind) const;

        std::deque<FToast> toasts_;
        FStyle style_{};
    };
}
