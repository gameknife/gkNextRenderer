#include "Engine/Runtime/Scene/SceneList.hpp"
#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Utilities/FileHelper.hpp"
#include "Engine/Assets/Data/Material.hpp"
#include "Engine/Assets/Loaders/FSceneLoader.h"
#include "Engine/Assets/Loaders/LoaderRegistry.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>

#include <spdlog/spdlog.h>

using namespace glm;

using Assets::Material;

namespace
{
    // glTF stays built into the engine core; every other format (.ldr/.mpd,
    // .scad, ...) and every demo ".proc" scene is registered into
    // Assets::FLoaderRegistry by the application / module that owns it.
    constexpr std::array<std::string_view, 2> kBuiltinSceneExtensions{
        ".glb",
        ".gltf",
    };

    std::string ToLowerCopy(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return value;
    }

    bool IsBuiltinSceneExtension(std::string_view extension)
    {
        return std::find(kBuiltinSceneExtensions.begin(), kBuiltinSceneExtensions.end(), extension)
            != kBuiltinSceneExtensions.end();
    }

    // Scene list grouping: procedural first, then glTF, then registered
    // formats in their registration order.
    int GetSceneSortKey(std::string_view scenePath)
    {
        const std::string extension = ToLowerCopy(std::filesystem::path(scenePath).extension().string());
        if (extension == ".proc")
        {
            return 0;
        }
        if (IsBuiltinSceneExtension(extension))
        {
            return 1;
        }
        const int order = Assets::FLoaderRegistry::Get().GetExtensionOrder(extension);
        return order >= 0 ? 2 + order : 1000;
    }

    void EmptyScene(Assets::EnvironmentSetting& cameraInit,
                    std::vector<std::shared_ptr<Assets::Node>>& /*nodes*/,
                    std::vector<Assets::Model>& /*models*/,
                    std::vector<Assets::FMaterial>& /*materials*/,
                    std::vector<Assets::LightObject>& /*lights*/,
                    std::vector<Assets::AnimationTrack>& /*tracks*/)
    {
        Assets::Camera defaultCam;
        defaultCam.name = "Cam";
        defaultCam.ModelView = lookAt(vec3(0, 4, 8), vec3(0, 0, 0), vec3(0, 1, 0));
        defaultCam.FieldOfView = 45;
        defaultCam.Aperture = 0;
        defaultCam.FocalDistance = 10;

        cameraInit.cameras.push_back(defaultCam);
        cameraInit.ControlSpeed = 5.0f;
        cameraInit.GammaCorrection = true;
        cameraInit.HasSky = true;
        cameraInit.HasSun = false;
    }
}

namespace Runtime::Scene
{

std::vector<std::string> SceneList::AllScenes;

bool SceneList::IsSupportedSceneExtension(std::string_view extension)
{
    if (extension.empty())
    {
        return false;
    }

    const std::string normalized = ToLowerCopy(std::string(extension));
    return IsBuiltinSceneExtension(normalized) || Assets::FLoaderRegistry::Get().SupportsExtension(normalized);
}

bool SceneList::IsSupportedScenePath(const std::filesystem::path& path)
{
    return path.has_extension() && IsSupportedSceneExtension(path.extension().string());
}

std::vector<std::string> SceneList::SupportedSceneExtensions()
{
    std::vector<std::string> extensions(kBuiltinSceneExtensions.begin(), kBuiltinSceneExtensions.end());
    for (std::string& registered : Assets::FLoaderRegistry::Get().RegisteredExtensions())
    {
        extensions.push_back(std::move(registered));
    }
    return extensions;
}

void SceneList::ScanScenes()
{
    AllScenes.clear();

    // add relative path
    std::string modelPath = "assets/models/";
    std::filesystem::path path = Utilities::FileHelper::GetPlatformFilePath(modelPath.c_str());

    // if with models, scan
    if (std::filesystem::exists(path))
    {
        SPDLOG_INFO("Scanning dir: {}", path.string());
        for (const auto& entry : std::filesystem::directory_iterator(path))
        {
            std::filesystem::path filename = entry.path().filename();
            if (!IsSupportedScenePath(entry.path())) continue;
            AllScenes.push_back((modelPath / filename).string());
        }
    }

    // Scan assets/omr/ for .ldr files
    std::string omrPath = "assets/omr/";
    std::filesystem::path omrDir = Utilities::FileHelper::GetPlatformFilePath(omrPath.c_str());
    if (std::filesystem::exists(omrDir))
    {
        for (const auto& entry : std::filesystem::directory_iterator(omrDir))
        {
            if (!IsSupportedScenePath(entry.path())) continue;
            std::filesystem::path filename = entry.path().filename();
            AllScenes.push_back((omrPath / filename).string());
        }
    }

    // Scan assets/scad/ for top-level .scad files (sub-modules under nested
    // directories are pulled in via `use`/`include`, not listed as scenes).
    std::string scadPath = "assets/scad/";
    std::filesystem::path scadDir = Utilities::FileHelper::GetPlatformFilePath(scadPath.c_str());
    if (std::filesystem::exists(scadDir))
    {
        for (const auto& entry : std::filesystem::directory_iterator(scadDir))
        {
            if (!entry.is_regular_file()) continue;
            if (!IsSupportedScenePath(entry.path())) continue;
            std::filesystem::path filename = entry.path().filename();
            AllScenes.push_back((scadPath / filename).string());
        }
    }

    // Pull additional top-level scene entries from any mounted paks (e.g. optional.pak),
    // so files moved out of the on-disk tree still appear in the scene list.
    auto* pakSystem = Utilities::Package::FPackageFileSystem::TryGetInstance();
    if (pakSystem != nullptr)
    {
        auto mergePakPrefix = [pakSystem](const std::string& prefix)
        {
            for (const auto& entry : pakSystem->ListMountedEntries(prefix))
            {
                // Only top-level files under the prefix (skip nested directories like KayKit/...).
                if (entry.find('/', prefix.size()) != std::string::npos) continue;
                if (!IsSupportedScenePath(std::filesystem::path(entry))) continue;
                AllScenes.push_back(entry);
            }
        };
        mergePakPrefix(modelPath);
        mergePakPrefix(omrPath);
    }

    // Procedural scenes come from the registry (registered by the application).
    for (const std::string& procName : Assets::FLoaderRegistry::Get().ProcSceneNames())
    {
        AllScenes.push_back(procName);
    }

    // Deduplicate (a file may exist on disk and in pak) before sorting.
    std::sort(AllScenes.begin(), AllScenes.end(), [](const std::string& lhs, const std::string& rhs)
    {
        const int lhsKey = GetSceneSortKey(lhs);
        const int rhsKey = GetSceneSortKey(rhs);
        if (lhsKey != rhsKey)
        {
            return lhsKey < rhsKey;
        }
        return lhs < rhs;
    });
    AllScenes.erase(std::unique(AllScenes.begin(), AllScenes.end()), AllScenes.end());

    SPDLOG_INFO("Scene found: {}", AllScenes.size());
}

int32_t SceneList::AddExternalScene(std::string absPath)
{
    // add absolute path
    if (std::filesystem::exists(absPath))
    {
        AllScenes.push_back(absPath);
    }
    return static_cast<int32_t>(AllScenes.size() - 1);
}

bool SceneList::LoadScene(std::string filename, Assets::EnvironmentSetting& camera, std::vector<std::shared_ptr<Assets::Node>>& nodes, std::vector<Assets::Model>& models,
                          std::vector<Assets::FMaterial>& materials,
                          std::vector<Assets::LightObject>& lights, std::vector<Assets::AnimationTrack>& tracks,
                          std::vector<Assets::Skeleton>& skeletons)
{
    std::filesystem::path filepath = filename;
    std::string ext = ToLowerCopy(filepath.extension().string());
    materials.push_back({Material::Lambertian(vec3(0.73f, 0.73f, 0.73f)), "root_default"});
    if (IsBuiltinSceneExtension(ext))
    {
        return Assets::FSceneLoader::LoadGLTFScene(filename, camera, nodes, models, materials, lights, tracks, skeletons);
    }
    if (ext == ".proc")
    {
        if (filename == "Empty.proc")
        {
            EmptyScene(camera, nodes, models, materials, lights, tracks);
            return true;
        }
        if (const Assets::FProcSceneFn* build = Assets::FLoaderRegistry::Get().FindProcScene(filename))
        {
            (*build)(camera, nodes, models, materials, lights, tracks);
            return true;
        }
        SPDLOG_ERROR("Unknown procedural scene (not registered): {}", filename);
        return false;
    }
    if (const Assets::FSceneLoaderFn* load = Assets::FLoaderRegistry::Get().FindSceneLoader(ext))
    {
        return (*load)(filename, camera, nodes, models, materials, lights, tracks, skeletons);
    }

    SPDLOG_ERROR("No scene loader registered for extension '{}': {}", ext, filename);
    return false;
}

}
