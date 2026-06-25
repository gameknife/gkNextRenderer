#include "Engine/Common/CoreMinimal.hpp"
#include "Modules/NextTui/TerminalIO.hpp"

#if !WIN32

#include <fcntl.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include <array>
#include <charconv>
#include <cstdio>

namespace Runtime::Tui
{
    struct TerminalIO::FPlatformState
    {
        termios InputState{};
        int InputFlags = 0;
        bool HasInputTty = false;
        bool HasOutputTty = false;
    };

    namespace
    {
        bool ParseUnsigned(std::string_view text, uint32_t& value)
        {
            const char* begin = text.data();
            const char* end = text.data() + text.size();
            const auto result = std::from_chars(begin, end, value);
            return result.ec == std::errc{} && result.ptr == end;
        }

        std::optional<FTerminalInputEvent> TryParseSgrMouseSequence(const std::string_view sequence)
        {
            if (sequence.size() < 6 || !sequence.starts_with("\x1b[<"))
            {
                return std::nullopt;
            }

            const char finalChar = sequence.back();
            if (finalChar != 'M' && finalChar != 'm')
            {
                return std::nullopt;
            }

            const std::string_view payload = sequence.substr(3, sequence.size() - 4);
            const size_t firstSeparator = payload.find(';');
            if (firstSeparator == std::string_view::npos)
            {
                return std::nullopt;
            }
            const size_t secondSeparator = payload.find(';', firstSeparator + 1);
            if (secondSeparator == std::string_view::npos)
            {
                return std::nullopt;
            }

            uint32_t cb = 0;
            uint32_t cx = 0;
            uint32_t cy = 0;
            if (!ParseUnsigned(payload.substr(0, firstSeparator), cb) ||
                !ParseUnsigned(payload.substr(firstSeparator + 1, secondSeparator - firstSeparator - 1), cx) ||
                !ParseUnsigned(payload.substr(secondSeparator + 1), cy))
            {
                return std::nullopt;
            }

            FTerminalInputEvent event{};
            event.Column = cx > 0 ? cx - 1 : 0;
            event.Row = cy > 0 ? cy - 1 : 0;

            if ((cb & 64u) != 0u)
            {
                event.Kind = ETerminalInputKind::MouseWheel;
                switch (cb & 0x3u)
                {
                case 0:
                    event.WheelY = 1.0f;
                    break;
                case 1:
                    event.WheelY = -1.0f;
                    break;
                case 2:
                    event.WheelX = 1.0f;
                    break;
                case 3:
                    event.WheelX = -1.0f;
                    break;
                default:
                    break;
                }
                return event;
            }

            switch (cb & 0x3u)
            {
            case 0:
                event.MouseButton = ETerminalMouseButton::Left;
                break;
            case 1:
                event.MouseButton = ETerminalMouseButton::Middle;
                break;
            case 2:
                event.MouseButton = ETerminalMouseButton::Right;
                break;
            default:
                event.MouseButton = ETerminalMouseButton::None;
                break;
            }

            if (finalChar == 'm')
            {
                event.Kind = ETerminalInputKind::MouseButtonUp;
                return event;
            }

            if ((cb & 32u) != 0u || event.MouseButton == ETerminalMouseButton::None)
            {
                event.Kind = ETerminalInputKind::MouseMove;
                return event;
            }

            event.Kind = ETerminalInputKind::MouseButtonDown;
            return event;
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
        platform_->HasOutputTty = ::isatty(STDOUT_FILENO) != 0;
        platform_->HasInputTty = captureInput_ && ::isatty(STDIN_FILENO) != 0;

        if (platform_->HasInputTty && tcgetattr(STDIN_FILENO, &platform_->InputState) == 0)
        {
            termios raw = platform_->InputState;
            cfmakeraw(&raw);
            tcsetattr(STDIN_FILENO, TCSANOW, &raw);
            platform_->InputFlags = fcntl(STDIN_FILENO, F_GETFL, 0);
            fcntl(STDIN_FILENO, F_SETFL, platform_->InputFlags | O_NONBLOCK);
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
        Write(platform_->HasInputTty
                  ? "\x1b[?1049h\x1b[?25l\x1b[?7l\x1b[?1002h\x1b[?1006h\x1b[2J\x1b[H"
                  : "\x1b[?1049h\x1b[?25l\x1b[?7l\x1b[2J\x1b[H");
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
            if (platform_->HasOutputTty)
            {
                Write(platform_->HasInputTty
                          ? "\x1b[0m\x1b[?1006l\x1b[?1002l\x1b[?7h\x1b[?25h\x1b[?1049l"
                          : "\x1b[0m\x1b[?7h\x1b[?25h\x1b[?1049l");
            }
            if (platform_->HasInputTty)
            {
                tcsetattr(STDIN_FILENO, TCSANOW, &platform_->InputState);
                fcntl(STDIN_FILENO, F_SETFL, platform_->InputFlags);
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
        winsize size{};
        if (::ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0 && size.ws_col > 0 && size.ws_row > 0)
        {
            return {
                .Columns = size.ws_col,
                .Rows = size.ws_row,
            };
        }
        return {};
    }

    std::vector<FTerminalInputEvent> TerminalIO::PollInput()
    {
        std::vector<FTerminalInputEvent> events;
        if (!started_ || !platform_ || !platform_->HasInputTty)
        {
            return events;
        }

        std::array<char, 64> buffer{};
        while (true)
        {
            const ssize_t readSize = ::read(STDIN_FILENO, buffer.data(), buffer.size());
            if (readSize <= 0)
            {
                break;
            }
            pendingInput_.append(buffer.data(), static_cast<size_t>(readSize));
        }

        while (!pendingInput_.empty())
        {
            const unsigned char first = static_cast<unsigned char>(pendingInput_[0]);
            if (first == 0x03)
            {
                events.push_back({.Kind = ETerminalInputKind::CtrlC});
                pendingInput_.erase(0, 1);
                continue;
            }
            if (first == 0x1b)
            {
                if (pendingInput_.size() < 3)
                {
                    break;
                }
                if (pendingInput_[1] == '[')
                {
                    if (pendingInput_.size() >= 4 && pendingInput_[2] == '<')
                    {
                        size_t sequenceEnd = pendingInput_.find_first_of("Mm", 3);
                        if (sequenceEnd == std::string::npos)
                        {
                            break;
                        }

                        const std::string_view sequence(pendingInput_.data(), sequenceEnd + 1);
                        if (const std::optional<FTerminalInputEvent> mouseEvent = TryParseSgrMouseSequence(sequence))
                        {
                            events.push_back(mouseEvent.value());
                            pendingInput_.erase(0, sequenceEnd + 1);
                            continue;
                        }
                    }

                    FTerminalInputEvent event{};
                    switch (pendingInput_[2])
                    {
                    case 'A':
                        event.Kind = ETerminalInputKind::ArrowUp;
                        break;
                    case 'B':
                        event.Kind = ETerminalInputKind::ArrowDown;
                        break;
                    case 'C':
                        event.Kind = ETerminalInputKind::ArrowRight;
                        break;
                    case 'D':
                        event.Kind = ETerminalInputKind::ArrowLeft;
                        break;
                    default:
                        pendingInput_.erase(0, 1);
                        continue;
                    }
                    events.push_back(event);
                    pendingInput_.erase(0, 3);
                    continue;
                }
            }

            events.push_back({
                .Kind = ETerminalInputKind::Character,
                .Character = pendingInput_[0],
            });
            pendingInput_.erase(0, 1);
        }

        return events;
    }

    bool TerminalIO::Write(std::string_view text) const
    {
        if (text.empty())
        {
            return true;
        }

        const bool ok = std::fwrite(text.data(), 1, text.size(), stdout) == text.size();
        std::fflush(stdout);
        return ok;
    }
}

#endif
