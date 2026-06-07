#pragma once

#include "Engine/Common/CoreMinimal.hpp"

#include <filesystem>
#include <string_view>

namespace NextPlatform::UserPaths
{
    std::filesystem::path GetUserDataDir(std::string_view appId);
    std::filesystem::path EnsureUserFile(std::string_view appId, std::string_view relativePath);
}
