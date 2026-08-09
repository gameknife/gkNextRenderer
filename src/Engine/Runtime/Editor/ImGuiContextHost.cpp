#include "Engine/Common/CoreMinimal.hpp"

#include "Engine/Runtime/Editor/ImGuiContextHost.hpp"

#include <imgui.h>

namespace NextUI
{
    FImGuiContextHost::~FImGuiContextHost()
    {
        Destroy();
    }

    void FImGuiContextHost::Create()
    {
        if (context_ == nullptr)
        {
            IMGUI_CHECKVERSION();
            context_ = ImGui::CreateContext();
        }
        MakeCurrent();
    }

    void FImGuiContextHost::Destroy()
    {
        if (context_ == nullptr)
        {
            return;
        }
        ImGui::DestroyContext(context_);
        context_ = nullptr;
    }

    void FImGuiContextHost::BeginFrame() const
    {
        MakeCurrent();
        ImGui::NewFrame();
    }

    bool FImGuiContextHost::WantsToCaptureKeyboard() const
    {
        MakeCurrent();
        return ImGui::GetIO().WantCaptureKeyboard;
    }

    bool FImGuiContextHost::WantsToCaptureMouse() const
    {
        MakeCurrent();
        return ImGui::GetIO().WantCaptureMouse;
    }

    void FImGuiContextHost::MakeCurrent() const
    {
        if (context_ != nullptr && ImGui::GetCurrentContext() != context_)
        {
            ImGui::SetCurrentContext(context_);
        }
    }
}
