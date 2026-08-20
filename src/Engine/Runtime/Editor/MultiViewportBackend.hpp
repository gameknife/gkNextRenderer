#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Vulkan/VulkanFwd.hpp"

#include <imgui.h>

namespace NextUI
{
    class IUserInterface;
    class UserInterface;

    class UiRenderBuffer final
    {
    public:
        UiRenderBuffer();
        ~UiRenderBuffer();
        UiRenderBuffer(UiRenderBuffer&&) noexcept;
        UiRenderBuffer& operator=(UiRenderBuffer&&) noexcept;

        UiRenderBuffer(const UiRenderBuffer&) = delete;
        UiRenderBuffer& operator=(const UiRenderBuffer&) = delete;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;

        friend class UserInterface;
    };

    class IMultiViewportBackend
    {
    public:
        virtual ~IMultiViewportBackend() = default;

        virtual void Initialize(IUserInterface& userInterface) = 0;
        virtual void Shutdown() = 0;
        virtual void OnUiPipelineDestroyed() = 0;
        virtual void RenderPlatformWindows() = 0;
    };
}
