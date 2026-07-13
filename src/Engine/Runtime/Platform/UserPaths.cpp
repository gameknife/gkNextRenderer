#include "Engine/Runtime/Platform/UserPaths.hpp"

#include <SDL3/SDL.h>

namespace NextPlatform::UserPaths
{
    std::filesystem::path GetUserDataDir(std::string_view appId)
    {
        char* prefPath = SDL_GetPrefPath("gkNextRenderer", std::string(appId).c_str());
        if (prefPath)
        {
            std::filesystem::path result(prefPath);
            SDL_free(prefPath);
            return result;
        }

        return std::filesystem::current_path() / std::string(appId);
    }

    std::filesystem::path EnsureUserFile(std::string_view appId, std::string_view relativePath)
    {
        std::filesystem::path path = GetUserDataDir(appId) / std::filesystem::path(std::string(relativePath));
        std::filesystem::create_directories(path.parent_path());
        return path;
    }
}
