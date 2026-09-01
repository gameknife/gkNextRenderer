#include "Engine/Runtime/Platform/UserPaths.hpp"

#include "Engine/Utilities/FileHelper.hpp"

#include <SDL3/SDL.h>
#include <spdlog/spdlog.h>

namespace
{
    std::filesystem::path NormalizeUserRelativePath(std::string_view input)
    {
        const std::filesystem::path source(input);
        if (source.is_absolute())
        {
            SPDLOG_WARN("Ignoring absolute user-data path '{}'; using its filename", input);
            return source.filename();
        }

        const std::filesystem::path normalized = source.lexically_normal();
        std::filesystem::path safePath;
        for (const std::filesystem::path& component : normalized)
        {
            if (component == "." || component.empty())
            {
                continue;
            }
            if (component == "..")
            {
                SPDLOG_WARN("Ignoring parent segment in user-data path '{}'", input);
                continue;
            }
            safePath /= component;
        }
        return safePath;
    }
}

namespace NextPlatform::UserPaths
{
    std::filesystem::path GetUserDataDir(std::string_view appId)
    {
        char* prefPath = SDL_GetPrefPath("gkNextRenderer", std::string(appId).c_str());
        if (prefPath != nullptr && prefPath[0] != '\0')
        {
            std::filesystem::path result(prefPath);
            SDL_free(prefPath);
            return result;
        }
        if (prefPath != nullptr)
        {
            SDL_free(prefPath);
        }

        return Utilities::FileHelper::GetWritableRuntimeRoot() / std::string(appId);
    }

    std::filesystem::path EnsureUserFile(std::string_view appId, std::string_view relativePath)
    {
        const std::filesystem::path safeRelativePath = NormalizeUserRelativePath(relativePath);
        std::filesystem::path path = GetUserDataDir(appId) / safeRelativePath;
        std::error_code errorCode;
        std::filesystem::create_directories(path.parent_path(), errorCode);
        if (errorCode)
        {
            SPDLOG_WARN("Failed to create user-data directory {}: {}",
                        path.parent_path().string(), errorCode.message());
        }
        return path;
    }
}
