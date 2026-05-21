#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Utilities/FileHelper.hpp"

#include <filesystem>

namespace Brotato3D
{
    // Official asset paths. SFX and icons live under assets/sounds/brotato3d and
    // assets/textures/brotato3d respectively, and are mounted at runtime via
    // assets/paks/brotato3d.pak. Paths returned here are project-root-relative; the
    // engine's package file system handles pak lookup and filesystem fallback.
    namespace Assets
    {
        inline std::string Sfx(const std::string& relPath)
        {
            return "assets/sounds/brotato3d/sfx/" + relPath;
        }

        inline std::string Icon(const std::string& category, const std::string& id)
        {
            return "assets/textures/brotato3d/icons/" + category + "/" + id + ".png";
        }
    }
}

namespace Brotato3D::PlaceholderAssets
{
    // Still-proprietary placeholder assets pending refresh. Once these categories are
    // refreshed (legitimate or self-authored content), they should follow the same
    // pattern as Brotato3D::Assets::Sfx/Icon and this namespace can be deleted.
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

    inline bool Exists(const std::string& fullPath)
    {
        return std::filesystem::exists(fullPath);
    }
}
