#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Runtime::Tui
{
    enum class ETerminalInputKind : uint8_t
    {
        Character,
        ArrowUp,
        ArrowDown,
        ArrowLeft,
        ArrowRight,
        CtrlC,
        MouseMove,
        MouseButtonDown,
        MouseButtonUp,
        MouseWheel,
    };

    enum class ETerminalMouseButton : uint8_t
    {
        None,
        Left,
        Middle,
        Right,
    };

    struct FTerminalInputEvent
    {
        ETerminalInputKind Kind = ETerminalInputKind::Character;
        char Character = '\0';
        ETerminalMouseButton MouseButton = ETerminalMouseButton::None;
        uint32_t Column = 0;
        uint32_t Row = 0;
        float WheelX = 0.0f;
        float WheelY = 0.0f;
    };

    struct FTerminalSize
    {
        uint32_t Columns = 80;
        uint32_t Rows = 24;
    };

    class TerminalIO final
    {
    public:
        explicit TerminalIO(bool captureInput);
        ~TerminalIO();

        bool Start();
        void Restore();
        FTerminalSize GetSize() const;
        std::vector<FTerminalInputEvent> PollInput();
        bool Write(std::string_view text) const;

    private:
        struct FPlatformState;

        bool captureInput_{};
        bool started_{};
        std::unique_ptr<FPlatformState> platform_{};
        std::string pendingInput_{};
    };
}
