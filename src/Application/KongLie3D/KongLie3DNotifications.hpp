#pragma once

#include "Common/CoreMinimal.hpp"

#include <deque>
#include <imgui.h>

namespace KongLie3D
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
        void Push(std::string text, ENotificationKind kind, float durationMs = 2500.0f);
        void Update(float deltaMs);
        void Clear();
        void Render(float bottomInset = 0.0f, float rightInset = 12.0f) const;

    private:
        std::deque<FToast> toasts_;
    };
}
