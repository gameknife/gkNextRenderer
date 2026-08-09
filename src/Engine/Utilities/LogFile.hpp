#pragma once

#include "Engine/Runtime/Platform/PlatformCommon.hpp"
#include "Engine/Utilities/FileHelper.hpp"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>

#include <string>

namespace Utilities::Logging
{
    inline std::string& LogFilePathStorage()
    {
        static std::string path;
        return path;
    }

    // Absolute path of the rotating log file, or an empty string when file logging
    // could not be started. Shown in the About dialog and in startup error dialogs so
    // a user reporting a bug can find something to attach.
    inline const std::string& GetLogFilePath()
    {
        return LogFilePathStorage();
    }

    // Attaches a rotating file sink to the default logger. Safe to call more than
    // once; only the first call installs a sink. Desktop only: Android logs through
    // logcat and iOS has no user-reachable filesystem for this.
    inline void InstallFileSink()
    {
#if !ANDROID && !IOS
        if (!LogFilePathStorage().empty())
        {
            return;
        }

        const std::string relativePath = "logs/" + NextRenderer::GetApplicationIdentity() + ".log";
        const std::string path = FileHelper::GetWritableFilePath(relativePath.c_str());
        try
        {
            constexpr size_t maxFileSize = 8 * 1024 * 1024;
            constexpr size_t maxFiles = 3;
            auto sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(path, maxFileSize, maxFiles);
            sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
            spdlog::default_logger()->sinks().push_back(sink);
            LogFilePathStorage() = path;
        }
        catch (const std::exception& error)
        {
            // A missing or read-only user directory must not stop the application.
            SPDLOG_WARN("file logging unavailable at '{}': {}", path, error.what());
        }
#endif
    }
}
