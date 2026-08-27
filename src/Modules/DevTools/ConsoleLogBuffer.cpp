#include "Modules/DevTools/ConsoleLogBuffer.hpp"

#include "Engine/Utilities/LogFormatting.hpp"

#include <algorithm>
#include <atomic>
#include <mutex>

#include <spdlog/sinks/callback_sink.h>
#include <spdlog/spdlog.h>

namespace Runtime::Editor
{
    namespace
    {
        std::mutex GConsoleLogSinkMutex;
        std::shared_ptr<ConsoleLogSink> GConsoleLogSink;
        std::shared_ptr<spdlog::sinks::callback_sink_mt> GConsoleSequenceSink;
        std::atomic<uint64_t> GConsoleLogSequence{0};
    } // namespace

    void AttachConsoleLogSinkToDefaultLogger(size_t maxLines)
    {
        const auto logger = spdlog::default_logger();
        if (!logger)
        {
            return;
        }

        std::lock_guard<std::mutex> lock(GConsoleLogSinkMutex);
        if (!GConsoleLogSink)
        {
            GConsoleLogSink = std::make_shared<ConsoleLogSink>(maxLines);
            // Stage milestones carry ANSI highlighting for the terminal; the ImGui console
            // renders escape codes as garbage, so strip them on the way into the buffer.
            GConsoleLogSink->set_formatter(
                std::make_unique<Utilities::Logging::FAnsiStrippingFormatter>("%+"));
        }
        if (!GConsoleSequenceSink)
        {
            GConsoleSequenceSink = std::make_shared<spdlog::sinks::callback_sink_mt>(
                [](const spdlog::details::log_msg&)
                {
                    GConsoleLogSequence.fetch_add(1, std::memory_order_relaxed);
                });
        }

        auto& sinks = logger->sinks();
        const bool ringExists =
            std::any_of(sinks.begin(), sinks.end(),
                        [](const std::shared_ptr<spdlog::sinks::sink>& sink) -> bool
                        { return sink.get() == GConsoleLogSink.get(); });
        if (!ringExists)
        {
            sinks.push_back(GConsoleLogSink);
        }
        const bool sequenceExists =
            std::any_of(sinks.begin(), sinks.end(),
                        [](const std::shared_ptr<spdlog::sinks::sink>& sink) -> bool
                        { return sink.get() == GConsoleSequenceSink.get(); });
        if (!sequenceExists)
        {
            sinks.push_back(GConsoleSequenceSink);
        }
    }

    std::shared_ptr<ConsoleLogSink> GetConsoleLogSink()
    {
        std::lock_guard<std::mutex> lock(GConsoleLogSinkMutex);
        return GConsoleLogSink;
    }

    uint64_t GetConsoleLogSequence()
    {
        return GConsoleLogSequence.load(std::memory_order_relaxed);
    }
} // namespace Runtime::Editor
