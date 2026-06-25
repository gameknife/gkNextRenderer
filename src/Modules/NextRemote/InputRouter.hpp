#pragma once

#include "Modules/NextRemote/VirtualGamepad.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct SDL_Window;

namespace Runtime::Remote
{
    class FInputRouter final
    {
    public:
        FInputRouter() = default;
        ~FInputRouter();

        void Stop();
        void HandleBinaryMessage(const std::vector<std::byte>& message);
        void HandleTextMessage(const std::string& message);

    private:
        void PushKey(bool down, uint16_t scancodeValue, uint16_t modValue, bool repeat);
        void PushMouseMove(uint8_t mode, float x, float y);
        void PushMouseButton(bool down, uint8_t button, float x, float y);
        void PushWheel(float x, float y);
        void ApplyGamepad(const std::byte* data, size_t size);

        // Synthetic events need the real SDL window id, otherwise ImGui's
        // ImGui_ImplSDL3_ProcessEvent drops mouse events whose windowID does not
        // map to a known viewport (windowID 0 from zero-initialized SDL_Event).
        // The engine is single-window, so resolve the main window lazily.
        static uint32_t ResolveMainWindowId();
        static SDL_Window* ResolveMainWindow();
        // Absolute mouse coords from the browser are normalized [0,1] over the
        // visible frame; ImGui reads window framebuffer pixels, so scale back.
        static void ScaleNormalizedToWindow(float& x, float& y);

        FVirtualGamepad virtualGamepad_;
    };
}
