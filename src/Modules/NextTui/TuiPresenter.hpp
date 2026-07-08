#pragma once

#include "Engine/Runtime/Interface/RenderFrameConsumer.hpp"
#include "Modules/NextTui/TerminalBlitter.hpp"
#include "Modules/NextTui/TerminalIO.hpp"

#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_scancode.h>
#include <vulkan/vulkan.h>

#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace Runtime::Config
{
    class Options;
}

class NextEngine;

namespace Modules::NextTui
{
    class FProcessLogCapture;
}

namespace Runtime::Tui
{
    class TuiPresenter final : public Runtime::IRenderFrameConsumer
    {
    public:
        TuiPresenter(NextEngine& engine, const Runtime::Config::Options& options);
        ~TuiPresenter() override;

        const char* Name() const override { return "TuiPresenter"; }
        bool Start() override;
        void RecordFrame(VkCommandBuffer commandBuffer, uint32_t imageIndex,
                         Vulkan::VulkanBaseRenderer& renderer) override;
        void Tick() override;
        void OnRendererDeleteSwapChain() override;

    private:
        struct FReadbackState;

        struct FKeyboardInput
        {
            SDL_Scancode Scancode = SDL_SCANCODE_UNKNOWN;
            SDL_Keycode Key = SDLK_UNKNOWN;
            SDL_Keymod Mod = SDL_KMOD_NONE;
        };

        struct FQueuedKeyRelease
        {
            SDL_Scancode Scancode = SDL_SCANCODE_UNKNOWN;
            SDL_Keycode Key = SDLK_UNKNOWN;
            SDL_Keymod Mod = SDL_KMOD_NONE;
            uint32_t ReleaseFrame = 0;
        };

        struct FRenderTargetSize
        {
            uint32_t Width = 0;
            uint32_t Height = 0;

            bool operator==(const FRenderTargetSize& other) const = default;
        };

        struct FViewportPointer
        {
            float X = 0.0f;
            float Y = 0.0f;
        };

        struct FInputState
        {
            std::vector<FQueuedKeyRelease> QueuedKeyReleases{};
            std::optional<FViewportPointer> LastPointerPosition{};
        };

        struct FResizeState
        {
            FTerminalSize LastTerminalSize{};
            FRenderTargetSize CurrentRenderTargetSize{};
            std::optional<FRenderTargetSize> PendingRenderTargetSize{};
            std::chrono::steady_clock::time_point LastTerminalSizePollTime{};
            std::chrono::steady_clock::time_point StableSince{};
        };

        struct FStatusState
        {
            std::string Message{};
            uint32_t UntilFrame = 0;
        };

        bool EnsureReadbackSlots();
        void HarvestCompletedReadbacks(uint64_t completedSubmitSerial);
        bool TryConsumeCapturedFrame(std::vector<FRgb8>& pixels, uint32_t& width, uint32_t& height);
        bool HasPendingCapture() const;
        void FlushQueuedKeyReleases();
        void HandleInput();
        void MaybeHandleTerminalResize(std::chrono::steady_clock::time_point now);
        std::optional<FViewportPointer> MapPointerToViewport(const FTerminalInputEvent& input) const;
        void QueueKeyPress(SDL_Scancode scancode, SDL_Keycode key, SDL_Keymod mod = SDL_KMOD_NONE);
        void RequestScreenshot();
        std::string BuildStatusLine(uint32_t columns, uint32_t rows) const;
        FRenderTargetSize ComputeRenderTargetSize(FTerminalSize terminalSize) const;
        std::optional<FKeyboardInput> MapCharacter(char value) const;
        void PushKeyEvent(SDL_Scancode scancode, SDL_Keycode key, SDL_Keymod mod, bool down) const;
        void PushMouseMotionEvent(const FViewportPointer& position, const FViewportPointer& delta) const;
        void PushMouseButtonEvent(ETerminalMouseButton button, bool down, const FViewportPointer& position) const;
        void PushMouseWheelEvent(float x, float y) const;

        NextEngine& engine_;
        const Runtime::Config::Options& options_;
        TerminalBlitter blitter_;
        TerminalIO terminal_;
        std::unique_ptr<Modules::NextTui::FProcessLogCapture> logCapture_;
        std::unique_ptr<FReadbackState> readback_;
        std::chrono::steady_clock::time_point lastPresentTime_{};
        FInputState inputState_{};
        FResizeState resizeState_{};
        FStatusState statusState_{};
    };
}
