#include "Engine/Runtime/Profiling/TracyIntegration.hpp"

namespace GkProfiling
{
    void SetThreadName(const char* name)
    {
#if GK_TRACY_ENABLED
        if (name != nullptr)
        {
            tracy::SetThreadName(name);
        }
#else
        (void)name;
#endif
    }

    void FrameMark()
    {
#if GK_TRACY_ENABLED
        tracy::Profiler::SendFrameMark(nullptr);
#endif
    }

    void AppInfo(const std::string_view text)
    {
#if GK_TRACY_ENABLED
        TracyAppInfo(text.data(), text.size());
#else
        (void)text;
#endif
    }

    void Message(const std::string_view text)
    {
#if GK_TRACY_ENABLED
        TracyMessage(text.data(), text.size());
#else
        (void)text;
#endif
    }

    void Plot(const char* name, const double value)
    {
#if GK_TRACY_ENABLED
        if (name != nullptr)
        {
            tracy::Profiler::PlotData(name, value);
        }
#else
        (void)name;
        (void)value;
#endif
    }

    void Plot(const char* name, const float value)
    {
#if GK_TRACY_ENABLED
        if (name != nullptr)
        {
            tracy::Profiler::PlotData(name, value);
        }
#else
        (void)name;
        (void)value;
#endif
    }

    void Plot(const char* name, const int64_t value)
    {
#if GK_TRACY_ENABLED
        if (name != nullptr)
        {
            tracy::Profiler::PlotData(name, value);
        }
#else
        (void)name;
        (void)value;
#endif
    }
}
