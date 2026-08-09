#pragma once

#include <filesystem>
#include <string>
#include <system_error>
#include <SDL3/SDL.h>

namespace NextRenderer
{
    // Short, filesystem-safe name of the running application. It names the per-user
    // data directory (see Utilities::FileHelper::GetWritableRuntimeRoot) and the log
    // file, so each shipped target keeps its own settings and layout.
    inline std::string& ApplicationIdentityStorage()
    {
        static std::string identity = "gkNextRenderer";
        return identity;
    }

    inline const std::string& GetApplicationIdentity()
    {
        return ApplicationIdentityStorage();
    }

    // Call once at process start, before any writable path is resolved.
    inline void SetApplicationIdentity(const std::filesystem::path& executablePath)
    {
        const std::string stem = executablePath.stem().string();
        if (!stem.empty())
        {
            ApplicationIdentityStorage() = stem;
        }
    }

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
