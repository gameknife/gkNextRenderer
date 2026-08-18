#pragma once

#include "Engine/Runtime/Profiling/FrameProfiler.hpp"
#include "Engine/Runtime/Profiling/TracyIntegration.hpp"

#ifndef GK_CONCAT_IMPL
#define GK_CONCAT_IMPL(a, b) a##b
#define GK_CONCAT(a, b) GK_CONCAT_IMPL(a, b)
#endif

#if GK_TRACY_ENABLED
#define GK_TRACY_CPU_ZONE(name) \
    GkProfiling::ScopedCpuZone GK_CONCAT(gkTracyCpuZone_, __LINE__)( \
        __LINE__, __FILE__, __FUNCTION__, name)
#else
#define GK_TRACY_CPU_ZONE(name)
#endif

#define SCOPED_GPU_TIMER_CMD(commandBufferValue, name) \
    Runtime::ScopedGpuMarker GK_CONCAT(scopedGpuMarker_, __LINE__)( \
        commandBufferValue, Runtime::FrameProfiler::GetActiveProfiler(), name); \
    Runtime::ScopedGpuProfileScope GK_CONCAT(scopedGpuTimer_, __LINE__)( \
        commandBufferValue, Runtime::FrameProfiler::GetActiveProfiler(), name)
#define SCOPED_GPU_TIMER(name) SCOPED_GPU_TIMER_CMD(commandBuffer, name)
#define SCOPED_CPU_TIMER(name) \
    PERFORMANCEAPI_INSTRUMENT_DATA(name, ""); \
    GK_TRACY_CPU_ZONE(name); \
    Runtime::ScopedCpuProfileScope GK_CONCAT(scopedCpuTimer_, __LINE__)( \
        Runtime::FrameProfiler::GetActiveProfiler(), name)
