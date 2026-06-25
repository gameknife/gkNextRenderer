#include "Engine/Common/CoreMinimal.hpp"
#include "Modules/NextTui/TerminalIO.hpp"

#if WIN32

#include <windows.h>

#include <array>
#include <cstdio>

namespace Runtime::Tui
{
    struct TerminalIO::FPlatformState
    {
        HANDLE Input = INVALID_HANDLE_VALUE;
        HANDLE Output = INVALID_HANDLE_VALUE;
        DWORD InputMode = 0;
        DWORD OutputMode = 0;
        UINT InputCodePage = 0;
        UINT OutputCodePage = 0;
        bool HasConsoleInput = false;
        bool HasConsoleOutput = false;
        DWORD PreviousMouseButtons = 0;
    };

    namespace
    {
        constexpr DWORD MouseButtonMask =
            FROM_LEFT_1ST_BUTTON_PRESSED | RIGHTMOST_BUTTON_PRESSED | FROM_LEFT_2ND_BUTTON_PRESSED;

        std::optional<ETerminalMouseButton> MapMouseButton(const DWORD stateBit)
        {
            switch (stateBit)
            {
            case FROM_LEFT_1ST_BUTTON_PRESSED:
                return ETerminalMouseButton::Left;
            case RIGHTMOST_BUTTON_PRESSED:
                return ETerminalMouseButton::Right;
            case FROM_LEFT_2ND_BUTTON_PRESSED:
                return ETerminalMouseButton::Middle;
            default:
                return std::nullopt;
            }
        }

        void SetMousePositionFromConsoleEvent(const HANDLE console,
                                             const MOUSE_EVENT_RECORD& mouse,
                                             FTerminalInputEvent& event)
        {
            CONSOLE_SCREEN_BUFFER_INFO info{};
            if (GetConsoleScreenBufferInfo(console, &info))
            {
                event.Column = mouse.dwMousePosition.X >= info.srWindow.Left
                    ? static_cast<uint32_t>(mouse.dwMousePosition.X - info.srWindow.Left)
                    : 0u;
                event.Row = mouse.dwMousePosition.Y >= info.srWindow.Top
                    ? static_cast<uint32_t>(mouse.dwMousePosition.Y - info.srWindow.Top)
                    : 0u;
                return;
            }

            event.Column = mouse.dwMousePosition.X >= 0 ? static_cast<uint32_t>(mouse.dwMousePosition.X) : 0u;
            event.Row = mouse.dwMousePosition.Y >= 0 ? static_cast<uint32_t>(mouse.dwMousePosition.Y) : 0u;
        }

        TerminalIO* GActiveTerminal = nullptr;

        void RestoreActiveTerminal()
        {
            if (GActiveTerminal)
            {
                GActiveTerminal->Restore();
            }
        }
    }

    TerminalIO::TerminalIO(bool captureInput)
        : captureInput_(captureInput)
    {
    }

    TerminalIO::~TerminalIO()
    {
        Restore();
    }

    bool TerminalIO::Start()
    {
        if (started_)
        {
            return true;
        }

        platform_ = std::make_unique<FPlatformState>();
        platform_->Input = GetStdHandle(STD_INPUT_HANDLE);
        platform_->Output = GetStdHandle(STD_OUTPUT_HANDLE);
        platform_->HasConsoleOutput = platform_->Output != INVALID_HANDLE_VALUE &&
            GetConsoleMode(platform_->Output, &platform_->OutputMode) != 0;
        platform_->HasConsoleInput = captureInput_ && platform_->Input != INVALID_HANDLE_VALUE &&
            GetConsoleMode(platform_->Input, &platform_->InputMode) != 0;

        if (platform_->HasConsoleOutput)
        {
            platform_->InputCodePage = GetConsoleCP();
            platform_->OutputCodePage = GetConsoleOutputCP();
            SetConsoleCP(CP_UTF8);
            SetConsoleOutputCP(CP_UTF8);
            SetConsoleMode(platform_->Output, platform_->OutputMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
        }

        if (platform_->HasConsoleInput)
        {
            DWORD rawMode = platform_->InputMode | ENABLE_WINDOW_INPUT | ENABLE_MOUSE_INPUT | ENABLE_EXTENDED_FLAGS;
            rawMode &= ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT | ENABLE_QUICK_EDIT_MODE);
            SetConsoleMode(platform_->Input, rawMode);
            FlushConsoleInputBuffer(platform_->Input);
        }

        static bool registered = false;
        if (!registered)
        {
            std::atexit(RestoreActiveTerminal);
            std::at_quick_exit(RestoreActiveTerminal);
            registered = true;
        }

        started_ = true;
        GActiveTerminal = this;
        Write("\x1b[?1049h\x1b[?25l\x1b[?7l\x1b[2J\x1b[H");
        return true;
    }

    void TerminalIO::Restore()
    {
        if (!started_)
        {
            return;
        }

        if (platform_)
        {
            if (platform_->HasConsoleOutput)
            {
                Write("\x1b[0m\x1b[?7h\x1b[?25h\x1b[?1049l");
                SetConsoleMode(platform_->Output, platform_->OutputMode);
                SetConsoleCP(platform_->InputCodePage);
                SetConsoleOutputCP(platform_->OutputCodePage);
            }
            if (platform_->HasConsoleInput)
            {
                SetConsoleMode(platform_->Input, platform_->InputMode);
            }
        }

        if (GActiveTerminal == this)
        {
            GActiveTerminal = nullptr;
        }
        started_ = false;
    }

    FTerminalSize TerminalIO::GetSize() const
    {
        if (platform_ && platform_->HasConsoleOutput)
        {
            CONSOLE_SCREEN_BUFFER_INFO info{};
            if (GetConsoleScreenBufferInfo(platform_->Output, &info))
            {
                FTerminalSize size{};
                size.Columns = static_cast<uint32_t>(info.srWindow.Right - info.srWindow.Left + 1);
                size.Rows = static_cast<uint32_t>(info.srWindow.Bottom - info.srWindow.Top + 1);
                return size;
            }
        }
        return {};
    }

    std::vector<FTerminalInputEvent> TerminalIO::PollInput()
    {
        std::vector<FTerminalInputEvent> events;
        if (!started_ || !platform_ || !platform_->HasConsoleInput)
        {
            return events;
        }

        DWORD pending = 0;
        if (!GetNumberOfConsoleInputEvents(platform_->Input, &pending) || pending == 0)
        {
            return events;
        }

        std::array<INPUT_RECORD, 16> records{};
        DWORD readCount = 0;
        if (!ReadConsoleInputW(platform_->Input, records.data(), static_cast<DWORD>(records.size()), &readCount))
        {
            return events;
        }

        for (DWORD i = 0; i < readCount; ++i)
        {
            const INPUT_RECORD& record = records[i];
            if (record.EventType == MOUSE_EVENT)
            {
                const MOUSE_EVENT_RECORD& mouse = record.Event.MouseEvent;
                const DWORD currentButtons = mouse.dwButtonState & MouseButtonMask;

                if (mouse.dwEventFlags == MOUSE_MOVED)
                {
                    FTerminalInputEvent event{.Kind = ETerminalInputKind::MouseMove};
                    SetMousePositionFromConsoleEvent(platform_->Output, mouse, event);
                    events.push_back(event);
                    platform_->PreviousMouseButtons = currentButtons;
                    continue;
                }

                if (mouse.dwEventFlags == MOUSE_WHEELED || mouse.dwEventFlags == MOUSE_HWHEELED)
                {
                    FTerminalInputEvent event{.Kind = ETerminalInputKind::MouseWheel};
                    SetMousePositionFromConsoleEvent(platform_->Output, mouse, event);
                    const short wheelDelta = static_cast<short>(HIWORD(mouse.dwButtonState));
                    const float normalized = wheelDelta > 0 ? 1.0f : (wheelDelta < 0 ? -1.0f : 0.0f);
                    if (mouse.dwEventFlags == MOUSE_WHEELED)
                    {
                        event.WheelY = normalized;
                    }
                    else
                    {
                        event.WheelX = normalized;
                    }
                    events.push_back(event);
                    continue;
                }

                if (mouse.dwEventFlags == 0 || mouse.dwEventFlags == DOUBLE_CLICK)
                {
                    const DWORD changedButtons = platform_->PreviousMouseButtons ^ currentButtons;
                    for (const DWORD buttonBit :
                         {FROM_LEFT_1ST_BUTTON_PRESSED, RIGHTMOST_BUTTON_PRESSED, FROM_LEFT_2ND_BUTTON_PRESSED})
                    {
                        if ((changedButtons & buttonBit) == 0)
                        {
                            continue;
                        }

                        const std::optional<ETerminalMouseButton> button = MapMouseButton(buttonBit);
                        if (!button.has_value())
                        {
                            continue;
                        }

                        FTerminalInputEvent event{
                            .Kind = (currentButtons & buttonBit) != 0
                                ? ETerminalInputKind::MouseButtonDown
                                : ETerminalInputKind::MouseButtonUp,
                            .MouseButton = button.value(),
                        };
                        SetMousePositionFromConsoleEvent(platform_->Output, mouse, event);
                        events.push_back(event);
                    }
                    platform_->PreviousMouseButtons = currentButtons;
                    continue;
                }

                platform_->PreviousMouseButtons = currentButtons;
                continue;
            }

            if (record.EventType != KEY_EVENT || !record.Event.KeyEvent.bKeyDown)
            {
                continue;
            }

            const KEY_EVENT_RECORD& key = record.Event.KeyEvent;
            switch (key.wVirtualKeyCode)
            {
            case VK_UP:
                events.push_back({.Kind = ETerminalInputKind::ArrowUp});
                continue;
            case VK_DOWN:
                events.push_back({.Kind = ETerminalInputKind::ArrowDown});
                continue;
            case VK_LEFT:
                events.push_back({.Kind = ETerminalInputKind::ArrowLeft});
                continue;
            case VK_RIGHT:
                events.push_back({.Kind = ETerminalInputKind::ArrowRight});
                continue;
            default:
                break;
            }

            const WCHAR unicode = key.uChar.UnicodeChar;
            if (unicode == 3 || ((key.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0 &&
                (key.wVirtualKeyCode == 'C' || key.wVirtualKeyCode == 'c')))
            {
                events.push_back({.Kind = ETerminalInputKind::CtrlC});
                continue;
            }

            if (unicode != 0 && unicode <= 0x7f)
            {
                events.push_back({
                    .Kind = ETerminalInputKind::Character,
                    .Character = static_cast<char>(unicode),
                });
            }
        }

        return events;
    }

    bool TerminalIO::Write(std::string_view text) const
    {
        if (text.empty())
        {
            return true;
        }

        if (platform_ && platform_->HasConsoleOutput)
        {
            DWORD written = 0;
            return WriteFile(platform_->Output, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) != 0;
        }

        const bool ok = std::fwrite(text.data(), 1, text.size(), stdout) == text.size();
        std::fflush(stdout);
        return ok;
    }
}

#endif
