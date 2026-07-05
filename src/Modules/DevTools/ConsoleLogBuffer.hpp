#pragma once

#include "Engine/Common/CoreMinimal.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <spdlog/sinks/ringbuffer_sink.h>

namespace Runtime::Editor
{
    using ConsoleLogSink = spdlog::sinks::ringbuffer_sink_mt;

    ENGINE_API void AttachConsoleLogSinkToDefaultLogger(size_t maxLines = 800);
    ENGINE_API std::shared_ptr<ConsoleLogSink> GetConsoleLogSink();
    ENGINE_API uint64_t GetConsoleLogSequence();
} // namespace Runtime::Editor
