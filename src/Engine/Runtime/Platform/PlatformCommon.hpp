#pragma once

#include <filesystem>
#include <system_error>
#include <SDL3/SDL.h>

namespace NextRenderer
{
    inline std::filesystem::path GetExecutableDirectory()
    {
        if (const char* basePath = SDL_GetBasePath(); basePath != nullptr && basePath[0] != '\0')
        {
            return std::filesystem::path(basePath).lexically_normal();
        }

        std::error_code errorCode;
        return std::filesystem::current_path(errorCode);
    }

    inline void NormalizeWorkingDirectoryToExecutableDirectory()
    {
        const std::filesystem::path executableDirectory = GetExecutableDirectory();
        if (executableDirectory.empty())
        {
            return;
        }

        std::error_code errorCode;
        std::filesystem::current_path(executableDirectory, errorCode);
    }
}

#if ANDROID
#include "Engine/Runtime/Platform/PlatformAndroid.hpp"
#elif WIN32
#include "Engine/Runtime/Platform/PlatformWindows.hpp"
#else
#include "Engine/Runtime/Platform/PlatformLinux.hpp"
#endif
