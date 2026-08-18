#pragma once

#include "Engine/Common/CoreMinimal.hpp"

#include <string_view>

#ifndef GK_TRACY_ENABLED
#define GK_TRACY_ENABLED 0
#endif

#if GK_TRACY_ENABLED
#include <tracy/tracy/Tracy.hpp>

// Tracy exposes FrameMark as a macro. Keep the engine-facing wrapper named
// FrameMark without allowing that macro to rewrite its declaration/call sites.
#ifdef FrameMark
#undef FrameMark
#endif
#endif

namespace GkProfiling
{
    inline size_t ZoneNameLength(const char* name)
    {
        return name == nullptr ? 0u : std::strlen(name);
    }

    class ScopedCpuZone final
    {
    public:
        ScopedCpuZone(uint32_t line, const char* source, const char* function, const char* name)
#if GK_TRACY_ENABLED
            : zone_(line,
                    source,
                    source == nullptr ? 0u : std::strlen(source),
                    function,
                    function == nullptr ? 0u : std::strlen(function),
                    name == nullptr ? "" : name,
                    ZoneNameLength(name),
                    0,
                    5,
                    true)
#endif
        {
#if !GK_TRACY_ENABLED
            (void)line;
            (void)source;
            (void)function;
            (void)name;
#endif
        }

        GK_NON_COPIABLE(ScopedCpuZone)

    private:
#if GK_TRACY_ENABLED
        tracy::ScopedZone zone_;
#endif
    };

    void SetThreadName(const char* name);
    void FrameMark();
    void AppInfo(std::string_view text);
    void Message(std::string_view text);
    void Plot(const char* name, double value);
    void Plot(const char* name, float value);
    void Plot(const char* name, int64_t value);
}
