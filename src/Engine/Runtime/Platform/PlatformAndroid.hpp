#pragma once

namespace NextRenderer
{
    inline void PlatformInit()
    {
        MarkProcessStart();
        NormalizeWorkingDirectoryToExecutableDirectory();
    }

    inline void HideConsole()
    {

    }

    inline void OSCommand(const char* command)
    {

    }

    inline int OSProcess(const char* exe)
    {
        (void)exe;
        return -1;
    }
}
