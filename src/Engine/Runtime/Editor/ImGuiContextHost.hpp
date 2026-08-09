#pragma once

#include "Engine/Common/CoreMinimal.hpp"

struct ImGuiContext;

namespace NextUI
{
    class FImGuiContextHost final
    {
    public:
        FImGuiContextHost() = default;
        ~FImGuiContextHost();
        GK_NON_COPIABLE(FImGuiContextHost)

        void Create();
        void Destroy();
        void BeginFrame() const;
        bool WantsToCaptureKeyboard() const;
        bool WantsToCaptureMouse() const;
        ImGuiContext* Context() const { return context_; }

    private:
        void MakeCurrent() const;
        ImGuiContext* context_ = nullptr;
    };
}
