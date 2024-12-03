#pragma once

namespace NextRenderer
{
    inline void PlatformInit()
    {
    
    }

    inline void HideConsole()
    {

    }

    inline void OSCommand(const char* command)
    {
        std::string commandline{"xdg-open "};
        commandline += command;
        system(commandline.c_str());
    }

    inline void OSProcess(const char* exe)
    {

    }
}

