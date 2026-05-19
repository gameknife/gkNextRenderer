#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Utilities/FileHelper.hpp"

#include <filesystem>

namespace Brotato3D::PlaceholderAssets
{
    inline constexpr const char* kPlaceholderRoot = "assets/_placeholder/brotato/";

    inline std::string Resolve(const std::string& relativePath)
    {
        const std::filesystem::path runtimeRoot = Utilities::FileHelper::GetRuntimeRoot();
        const std::filesystem::path runtimeCandidate =
            std::filesystem::path(Utilities::FileHelper::GetPlatformFilePath(relativePath.c_str())).lexically_normal();
        if (std::filesystem::exists(runtimeCandidate))
        {
            return runtimeCandidate.string();
        }

        const std::filesystem::path repoCandidate =
            (runtimeRoot / ".." / ".." / ".." / relativePath).lexically_normal();
        if (std::filesystem::exists(repoCandidate))
        {
            return repoCandidate.string();
        }

        return runtimeCandidate.string();
    }

    inline std::string Root()
    {
        return Resolve(kPlaceholderRoot);
    }

    inline std::string Sfx(const std::string& relPath)
    {
        return Resolve(std::string(kPlaceholderRoot) + "audio/sfx/" + relPath);
    }

    inline std::string Bgm(const std::string& relPath)
    {
        return Resolve(std::string(kPlaceholderRoot) + "audio/bgm/" + relPath);
    }

    inline std::string Font(const std::string& relPath)
    {
        return Resolve(std::string(kPlaceholderRoot) + "fonts/" + relPath);
    }

    inline std::string Hud(const std::string& relPath)
    {
        return Resolve(std::string(kPlaceholderRoot) + "ui/hud/" + relPath);
    }

    inline std::string Menu(const std::string& relPath)
    {
        return Resolve(std::string(kPlaceholderRoot) + "ui/menu/" + relPath);
    }

    inline std::string Icon(const std::string& category, const std::string& id)
    {
        return Resolve(std::string(kPlaceholderRoot) + "ui/icons/" + category + "/" + id + ".png");
    }

    inline bool Exists(const std::string& fullPath)
    {
        return std::filesystem::exists(fullPath);
    }
}
