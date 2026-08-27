#pragma once

#include <spdlog/spdlog.h>
#include <spdlog/pattern_formatter.h>

#include <memory>
#include <string>
#include <string_view>
#include <utility>

#if WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace Utilities::Logging
{
    // True when stdout is a terminal that can render SGR sequences. spdlog's own color sink
    // makes the same check before emitting colors, so a redirected stream (gnb capturing the
    // build/run log, a CI pipe) stays plain text.
    inline bool StdoutSupportsAnsi()
    {
        static const bool supported = []
        {
#if WIN32
            // PlatformInit enables ENABLE_VIRTUAL_TERMINAL_PROCESSING on the console handle.
            return ::_isatty(::_fileno(stdout)) != 0;
#else
            return ::isatty(::fileno(stdout)) != 0;
#endif
        }();
        return supported;
    }

    // Bright cyan + bold. Empty when stdout is not a terminal.
    inline const char* StageColorBegin()
    {
        return StdoutSupportsAnsi() ? "\x1b[1;96m" : "";
    }

    inline const char* StageColorEnd()
    {
        return StdoutSupportsAnsi() ? "\x1b[0m" : "";
    }

    // Copies text into dest with CSI escape sequences removed.
    inline void AppendWithoutAnsi(std::string_view text, spdlog::memory_buf_t& dest)
    {
        size_t plainBegin = 0;
        size_t index = 0;
        while (index < text.size())
        {
            if (text[index] != '\x1b')
            {
                ++index;
                continue;
            }

            dest.append(text.data() + plainBegin, text.data() + index);

            size_t scan = index + 1;
            if (scan < text.size() && text[scan] == '[')
            {
                ++scan;
                // Parameter and intermediate bytes run until the final byte in '@'..'~'.
                while (scan < text.size() && (text[scan] < '@' || text[scan] > '~'))
                {
                    ++scan;
                }
                if (scan < text.size())
                {
                    ++scan;
                }
            }
            index = scan;
            plainBegin = scan;
        }
        dest.append(text.data() + plainBegin, text.data() + text.size());
    }

    // Pattern formatter that drops ANSI escapes. Sinks that are not a terminal - the rotating
    // log file and the in-app console ring buffer - use this so highlighted messages do not
    // reach them as literal escape codes.
    class FAnsiStrippingFormatter final : public spdlog::formatter
    {
    public:
        explicit FAnsiStrippingFormatter(std::string pattern)
            : pattern_(std::move(pattern))
            , inner_(std::make_unique<spdlog::pattern_formatter>(pattern_))
        {
        }

        void format(const spdlog::details::log_msg& message, spdlog::memory_buf_t& dest) override
        {
            spdlog::memory_buf_t formatted;
            inner_->format(message, formatted);
            AppendWithoutAnsi(std::string_view(formatted.data(), formatted.size()), dest);
        }

        std::unique_ptr<spdlog::formatter> clone() const override
        {
            return std::make_unique<FAnsiStrippingFormatter>(pattern_);
        }

    private:
        std::string pattern_;
        std::unique_ptr<spdlog::pattern_formatter> inner_;
    };
}

// Startup/shutdown milestones ("---- ..."). Highlighted on a terminal, plain everywhere else.
#define GK_LOG_STAGE(...)                                                    \
    SPDLOG_INFO("{}{}{}",                                                    \
                ::Utilities::Logging::StageColorBegin(),                     \
                ::fmt::format(__VA_ARGS__),                                  \
                ::Utilities::Logging::StageColorEnd())
