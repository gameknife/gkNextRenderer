#pragma once

#include <cstdlib>
#include <string>

namespace NextRenderer
{
    inline void PlatformInit()
    {
        NormalizeWorkingDirectoryToExecutableDirectory();
    }

    inline void HideConsole()
    {

    }

    inline int OSProcess(const char* exe)
    {
#if IOS || ANDROID
        return 1;
#else
        return std::system(exe);
#endif
    }

    inline void OSCommand(const char* command)
    {
        std::string commandline{"xdg-open "};
        commandline += command;
        OSProcess(commandline.c_str());
    }
}
