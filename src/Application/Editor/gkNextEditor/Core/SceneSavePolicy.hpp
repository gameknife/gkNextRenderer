#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Utilities/FileHelper.hpp"

namespace Editor
{
    inline bool IsWritableGltfScenePath(const std::filesystem::path& path)
    {
        const std::string extension = path.extension().string();
        return extension == ".glb" || extension == ".GLB" ||
            extension == ".gltf" || extension == ".GLTF";
    }

    inline bool IsPakScenePath(const std::string& path)
    {
        auto* pakSystem = Utilities::Package::FPackageFileSystem::TryGetInstance();
        return pakSystem && pakSystem->HasMountedEntry(path);
    }

    inline std::filesystem::path ResolveSceneFilesystemPath(const std::string& path)
    {
        if (path.empty())
        {
            return {};
        }

        std::error_code ec;
        const std::filesystem::path directPath(path);
        if (directPath.is_absolute())
        {
            return directPath;
        }

        const std::filesystem::path runtimePath = Utilities::FileHelper::GetRuntimeFilePath(path);
        if (std::filesystem::exists(runtimePath, ec))
        {
            return runtimePath;
        }

        if (std::filesystem::exists(directPath, ec))
        {
            return directPath;
        }

        return directPath;
    }

    inline bool CanOverwriteCurrentScene(const std::string& path)
    {
        if (path.empty() || !IsWritableGltfScenePath(path))
        {
            return false;
        }

        std::error_code ec;
        const std::filesystem::path filesystemPath = ResolveSceneFilesystemPath(path);
        const bool hasFilesystemFile = std::filesystem::exists(filesystemPath, ec)
            && std::filesystem::is_regular_file(filesystemPath, ec);
        if (!hasFilesystemFile)
        {
            return false;
        }

        // A scene may exist both in assets/ on disk and in optional.pak. That is still writable:
        // only pak-only scenes are considered non-writable.
        return std::filesystem::exists(filesystemPath, ec)
            && std::filesystem::is_regular_file(filesystemPath, ec);
    }

    inline std::filesystem::path DefaultSceneSaveDirectory()
    {
        std::error_code ec;
        const std::filesystem::path sourceAssets = std::filesystem::absolute("assets", ec);
        if (!ec && std::filesystem::exists(sourceAssets, ec))
        {
            return sourceAssets;
        }

        return Utilities::FileHelper::GetWritableRuntimeRoot() / "assets";
    }

    inline std::string NormalizeSaveAsScenePath(std::string path, const std::string_view defaultExtension = ".glb")
    {
        if (path.empty())
        {
            return {};
        }

        std::filesystem::path filesystemPath(path);
        if (!filesystemPath.has_extension())
        {
            filesystemPath.replace_extension(defaultExtension);
        }
        return filesystemPath.string();
    }
}
