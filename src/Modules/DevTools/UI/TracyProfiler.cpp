#include "Engine/Common/CoreMinimal.hpp"

#include "TracyProfiler.hpp"

#include "Engine/Runtime/Platform/PlatformCommon.hpp"

#include <SDL3/SDL_process.h>

#include <algorithm>
#include <cctype>

namespace Runtime::DevToolsUI
{
    namespace
    {
        constexpr const char* tracyAddress = "127.0.0.1";
        constexpr const char* tracyPort = "8086";

        std::filesystem::path FindTracyRoot()
        {
            std::array<std::filesystem::path, 2> starts{};
            starts[0] = NextRenderer::GetExecutableDirectory();

            std::error_code currentPathError;
            starts[1] = std::filesystem::current_path(currentPathError);
            if (currentPathError)
            {
                starts[1].clear();
            }

            for (const std::filesystem::path& start : starts)
            {
                if (start.empty())
                {
                    continue;
                }

                std::filesystem::path cursor = start.lexically_normal();
                while (!cursor.empty())
                {
                    const std::filesystem::path tracyRoot = cursor / "external" / "tracy";
                    std::error_code rootError;
                    if (std::filesystem::is_directory(tracyRoot, rootError))
                    {
                        return tracyRoot;
                    }

                    const std::filesystem::path parent = cursor.parent_path();
                    if (parent == cursor)
                    {
                        break;
                    }
                    cursor = parent;
                }
            }

            return {};
        }

        std::filesystem::path FindTracyProfiler()
        {
#if defined(GK_TRACY_ENABLED) && GK_TRACY_ENABLED
            const std::filesystem::path tracyRoot = FindTracyRoot();
            if (tracyRoot.empty())
            {
                return {};
            }

            std::error_code iteratorError;
            const auto iteratorOptions = std::filesystem::directory_options::skip_permission_denied;
            for (const auto& entry : std::filesystem::recursive_directory_iterator(
                     tracyRoot, iteratorOptions, iteratorError))
            {
                if (iteratorError)
                {
                    iteratorError.clear();
                    continue;
                }

                std::error_code fileError;
                if (!entry.is_regular_file(fileError))
                {
                    continue;
                }

                std::string filename = entry.path().filename().string();
                std::transform(filename.begin(), filename.end(), filename.begin(),
                               [](const unsigned char value) { return static_cast<char>(std::tolower(value)); });
                if (filename == "tracy.exe" || filename == "tracy-profiler.exe" || filename == "tracy-profiler")
                {
                    return entry.path();
                }
            }
#endif
            return {};
        }
    }

    bool IsTracyProfilerAvailable()
    {
        return !FindTracyProfiler().empty();
    }

    bool LaunchTracyProfiler()
    {
#if defined(GK_TRACY_ENABLED) && GK_TRACY_ENABLED && !ANDROID && !IOS
        const std::filesystem::path profiler = FindTracyProfiler();
        if (profiler.empty())
        {
            SPDLOG_WARN("Tracy profiler GUI was not found; run `gnb tracy fetch` first");
            return false;
        }

        const std::string profilerPath = profiler.string();
        const char* args[] = {profilerPath.c_str(), "-a", tracyAddress, "-p", tracyPort, nullptr};
        SDL_Process* process = SDL_CreateProcess(args, false);
        if (process == nullptr)
        {
            SPDLOG_ERROR("Failed to launch Tracy profiler {}: {}", profilerPath, SDL_GetError());
            return false;
        }

        SDL_DestroyProcess(process);
        SPDLOG_INFO("Launched Tracy profiler and connecting to {}:{}", tracyAddress, tracyPort);
        return true;
#else
        return false;
#endif
    }
}
