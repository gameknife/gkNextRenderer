#include "Engine/Runtime/Scene/SceneList.hpp"
#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Utilities/FileHelper.hpp"
#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Assets/Data/Material.hpp"
#include "Engine/Assets/Loaders/LoaderRegistry.hpp"
#include "Engine/Runtime/Components/EnvironmentComponent.hpp"
#include "Engine/Runtime/Components/RenderComponent.hpp"
#include "Engine/Runtime/Components/SceneReferenceComponent.hpp"

#include <algorithm>
#include <cctype>


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

    bool IsReferenceLeafOrHostExtension(const std::filesystem::path& path)
    {
        const std::string filename = ToLowerCopy(path.filename().string());
        if (filename == "meta.json")
        {
            return true;
        }

        const std::string ext = ToLowerCopy(path.extension().string());
        return ext == ".gltf" || ext == ".glb" || ext == ".scad" || ext == ".sog" ||
            ext == ".ldr" || ext == ".mpd";
    }

    std::string CanonicalCycleKey(std::string path)
    {
        std::replace(path.begin(), path.end(), '\\', '/');
        path = Utilities::FileHelper::NormalizePathString(path);
#if WIN32
        path = ToLowerCopy(path);
#endif
        return path;
    }

    bool NormalizeReferenceAssetPath(const std::string& input, std::string& outPath, std::string& outError)
    {
        std::string normalizedInput = input;
        std::replace(normalizedInput.begin(), normalizedInput.end(), '\\', '/');

        const std::filesystem::path rawPath(normalizedInput);
        if (rawPath.is_absolute())
        {
            outError = "Scene references must use relative assets/ paths";
            return false;
        }

        outPath = Utilities::FileHelper::NormalizePathString(normalizedInput);
        if (outPath.empty() || outPath.find("..") != std::string::npos)
        {
            outError = "Scene reference path cannot be empty or contain '..'";
            return false;
        }
        if (outPath.rfind("assets/", 0) != 0)
        {
            outError = "Scene reference path must start with assets/";
            return false;
        }
        return true;
    }

    uint32_t GenerateInstanceIdFromNodes(const std::vector<std::shared_ptr<Assets::Node>>& nodes)
    {
        uint32_t maxId = 0;
        for (const auto& node : nodes)
        {
            maxId = std::max(maxId, node->GetInstanceId());
        }
        return nodes.empty() ? 0 : maxId + 1;
    }

    Runtime::EnvironmentComponent* FindEnvironmentComponent(
        const std::vector<std::shared_ptr<Assets::Node>>& nodes,
        bool includeSceneReferenceInternal = false)
    {
        for (const auto& node : nodes)
        {
            if (!node || (!includeSceneReferenceInternal && node->IsSceneReferenceInternal()))
            {
                continue;
            }
            if (auto* environment = node->GetComponent<Runtime::EnvironmentComponent>())
            {
                return environment;
            }
        }
        return nullptr;
    }

    void EnsureEnvironmentComponentNode(Assets::EnvironmentSetting& environment,
                                        std::vector<std::shared_ptr<Assets::Node>>& nodes)
    {
        if (auto* component = FindEnvironmentComponent(nodes))
        {
            if (component->cameras.empty())
            {
                component->cameras = environment.cameras;
            }
            environment = component->GetSettings();
            return;
        }

        auto node = Assets::Node::CreateNode("Environment", glm::vec3(0.0f),
                                             glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
                                             glm::vec3(1.0f), GenerateInstanceIdFromNodes(nodes));
        auto component = std::make_shared<Runtime::EnvironmentComponent>();
        component->SetSettings(environment);
        node->AddComponent(component);
        nodes.push_back(node);
    }

    struct FSceneReferenceLoadContext
    {
        std::vector<std::string> stack;
        uint32_t depth = 0;
        uint32_t maxDepth = 16;
    };

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
    if (ToLowerCopy(path.filename().string()) == "meta.json")
    {
        return Assets::FLoaderRegistry::Get().FindSceneLoader(".sog") != nullptr;
    }
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
            const std::string scenePath = (modelPath / filename).string();
            if (!Utilities::FileHelper::IsAssetAvailable(scenePath)) continue;
            AllScenes.push_back(scenePath);
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
            const std::string scenePath = (omrPath / filename).string();
            if (!Utilities::FileHelper::IsAssetAvailable(scenePath)) continue;
            AllScenes.push_back(scenePath);
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
            const std::string scenePath = (scadPath / filename).string();
            if (!Utilities::FileHelper::IsAssetAvailable(scenePath)) continue;
            AllScenes.push_back(scenePath);
        }
    }

    // SOG supports both packaged *.sog files and unpacked directories whose
    // entry point is meta.json. Nested unpacked datasets are listed as scenes.
    const std::string sogPath = "assets/sog/";
    const std::filesystem::path sogDir = Utilities::FileHelper::GetPlatformFilePath(sogPath.c_str());
    if (std::filesystem::exists(sogDir))
    {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(sogDir))
        {
            if (!entry.is_regular_file() || !IsSupportedScenePath(entry.path())) continue;
            const std::string scenePath = (std::filesystem::path(sogPath) /
                std::filesystem::relative(entry.path(), sogDir)).generic_string();
            if (!Utilities::FileHelper::IsAssetAvailable(scenePath)) continue;
            AllScenes.push_back(scenePath);
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

namespace
{
bool LoadSceneRaw(std::string filename, Assets::EnvironmentSetting& camera,
                  std::vector<std::shared_ptr<Assets::Node>>& nodes, std::vector<Assets::Model>& models,
                  std::vector<Assets::FMaterial>& materials,
                  std::vector<Assets::LightObject>& lights, std::vector<Assets::AnimationTrack>& tracks,
                  std::vector<Assets::Skeleton>& skeletons)
{
    std::filesystem::path filepath = filename;
    std::string ext = ToLowerCopy(filepath.extension().string());
    materials.push_back({Material::Lambertian(vec3(0.73f, 0.73f, 0.73f)), "root_default"});
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
    if (ToLowerCopy(filepath.filename().string()) == "meta.json")
    {
        if (const Assets::FSceneLoaderFn* load = Assets::FLoaderRegistry::Get().FindSceneLoader(".sog"))
        {
            return (*load)(filename, camera, nodes, models, materials, lights, tracks, skeletons);
        }
    }

    SPDLOG_ERROR("No scene loader registered for extension '{}': {}", ext, filename);
    return false;
}

void ResolveSceneReferences(Assets::EnvironmentSetting& camera, std::vector<std::shared_ptr<Assets::Node>>& nodes,
                            std::vector<Assets::Model>& models, std::vector<Assets::FMaterial>& materials,
                            std::vector<Assets::LightObject>& lights, std::vector<Assets::AnimationTrack>& tracks,
                            std::vector<Assets::Skeleton>& skeletons,
                            FSceneReferenceLoadContext& context);

bool ResolveSceneReferenceProxy(const std::shared_ptr<Assets::Node>& proxy, Assets::EnvironmentSetting& camera,
                                std::vector<std::shared_ptr<Assets::Node>>& nodes,
                                std::vector<Assets::Model>& models, std::vector<Assets::FMaterial>& materials,
                                std::vector<Assets::LightObject>& lights,
                                std::vector<Assets::AnimationTrack>& tracks,
                                std::vector<Assets::Skeleton>& skeletons,
                                FSceneReferenceLoadContext& context)
{
    auto sceneReference = proxy->GetComponent<Runtime::SceneReferenceComponent>();
    if (!sceneReference)
    {
        return false;
    }

    auto fail = [&sceneReference](Runtime::ESceneReferenceStatus status, const std::string& error)
    {
        sceneReference->SetStatus(status);
        sceneReference->SetError(error);
        sceneReference->SetLoadedNodeCount(0);
        SPDLOG_WARN("Scene reference failed: {}", error);
        return false;
    };

    std::string assetPath;
    std::string error;
    if (!NormalizeReferenceAssetPath(sceneReference->GetAssetPath(), assetPath, error))
    {
        return fail(Runtime::ESceneReferenceStatus::Unsupported, error);
    }

    const std::filesystem::path assetFsPath(assetPath);
    if (!IsReferenceLeafOrHostExtension(assetFsPath))
    {
        return fail(Runtime::ESceneReferenceStatus::Unsupported,
                    fmt::format("Unsupported scene reference extension: {}", assetFsPath.extension().string()));
    }

    const std::string cycleKey = CanonicalCycleKey(assetPath);
    if (std::find(context.stack.begin(), context.stack.end(), cycleKey) != context.stack.end())
    {
        return fail(Runtime::ESceneReferenceStatus::CycleDetected,
                    fmt::format("Scene reference cycle detected at {}", assetPath));
    }
    if (context.depth >= context.maxDepth)
    {
        return fail(Runtime::ESceneReferenceStatus::Failed,
                    fmt::format("Scene reference nesting exceeds {}", context.maxDepth));
    }

    Assets::EnvironmentSetting localCamera;
    std::vector<std::shared_ptr<Assets::Node>> localNodes;
    std::vector<Assets::Model> localModels;
    std::vector<Assets::FMaterial> localMaterials;
    std::vector<Assets::LightObject> localLights;
    std::vector<Assets::AnimationTrack> localTracks;
    std::vector<Assets::Skeleton> localSkeletons;

    FSceneReferenceLoadContext childContext = context;
    childContext.stack.push_back(cycleKey);
    childContext.depth++;

    if (!LoadSceneRaw(assetPath, localCamera, localNodes, localModels, localMaterials, localLights,
                      localTracks, localSkeletons))
    {
        const auto status = Utilities::FileHelper::IsAssetAvailable(assetPath)
            ? Runtime::ESceneReferenceStatus::Failed
            : Runtime::ESceneReferenceStatus::Missing;
        return fail(status, fmt::format("Failed to load referenced scene: {}", assetPath));
    }

    const std::string ext = ToLowerCopy(assetFsPath.extension().string());
    if (ext == ".gltf" || ext == ".glb")
    {
        ResolveSceneReferences(localCamera, localNodes, localModels, localMaterials, localLights,
                               localTracks, localSkeletons, childContext);
    }

    if (!localLights.empty() || !localTracks.empty() || !localSkeletons.empty())
    {
        SPDLOG_WARN("Scene reference '{}' currently ignores local lights/animations/skinning", assetPath);
    }

    const uint32_t modelOffset = static_cast<uint32_t>(models.size());
    const uint32_t materialOffset = static_cast<uint32_t>(materials.size());
    uint32_t nextInstanceId = GenerateInstanceIdFromNodes(nodes);
    std::unordered_map<uint32_t, uint32_t> nodeIdRemap;
    nodeIdRemap.reserve(localNodes.size());

    for (const auto& localNode : localNodes)
    {
        const uint32_t oldId = localNode->GetInstanceId();
        const uint32_t newId = nextInstanceId++;
        nodeIdRemap[oldId] = newId;
        localNode->SetInstanceId(newId);
        localNode->SetSceneReferenceOwnerProxyId(proxy->GetInstanceId());
        localNode->SetName(fmt::format("__ref_{}__/{}", proxy->GetInstanceId(), localNode->GetName()));

        if (auto render = localNode->GetComponent<Runtime::RenderComponent>())
        {
            if (render->IsDrawable())
            {
                render->SetModelId(render->GetModelId() + modelOffset);
            }
            auto mats = render->GetMaterials();
            for (uint32_t& materialId : mats)
            {
                materialId += materialOffset;
            }
            render->SetMaterials(mats);
            render->SetSkinIndex(-1);
        }

    }

    for (const auto& localNode : localNodes)
    {
        if (localNode->GetParent() == nullptr)
        {
            localNode->SetParent(proxy);
        }
    }

    models.reserve(models.size() + localModels.size());
    for (auto& model : localModels)
    {
        models.emplace_back(std::move(model));
    }
    materials.reserve(materials.size() + localMaterials.size());
    for (auto& material : localMaterials)
    {
        materials.emplace_back(std::move(material));
    }
    nodes.reserve(nodes.size() + localNodes.size());
    for (auto& node : localNodes)
    {
        nodes.emplace_back(std::move(node));
    }

    sceneReference->SetResolvedPath(assetPath);
    sceneReference->SetStatus(Runtime::ESceneReferenceStatus::Loaded);
    sceneReference->SetError({});
    sceneReference->SetLoadedNodeCount(static_cast<uint32_t>(nodeIdRemap.size()));
    return true;
}

void ResolveSceneReferences(Assets::EnvironmentSetting& camera, std::vector<std::shared_ptr<Assets::Node>>& nodes,
                            std::vector<Assets::Model>& models, std::vector<Assets::FMaterial>& materials,
                            std::vector<Assets::LightObject>& lights, std::vector<Assets::AnimationTrack>& tracks,
                            std::vector<Assets::Skeleton>& skeletons,
                            FSceneReferenceLoadContext& context)
{
    const size_t originalNodeCount = nodes.size();
    for (size_t i = 0; i < originalNodeCount; ++i)
    {
        const auto& node = nodes[i];
        if (!node || node->IsSceneReferenceInternal())
        {
            continue;
        }
        if (node->GetComponent<Runtime::SceneReferenceComponent>())
        {
            ResolveSceneReferenceProxy(node, camera, nodes, models, materials, lights, tracks, skeletons, context);
        }
    }
}

bool LoadSceneWithReferences(std::string filename, Assets::EnvironmentSetting& camera,
                             std::vector<std::shared_ptr<Assets::Node>>& nodes,
                             std::vector<Assets::Model>& models,
                             std::vector<Assets::FMaterial>& materials,
                             std::vector<Assets::LightObject>& lights,
                             std::vector<Assets::AnimationTrack>& tracks,
                             std::vector<Assets::Skeleton>& skeletons)
{
    if (!LoadSceneRaw(filename, camera, nodes, models, materials, lights, tracks, skeletons))
    {
        return false;
    }

    FSceneReferenceLoadContext context;
    std::string normalizedRoot;
    std::string ignoredError;
    context.stack.push_back(NormalizeReferenceAssetPath(filename, normalizedRoot, ignoredError)
                                ? CanonicalCycleKey(normalizedRoot)
                                : CanonicalCycleKey(filename));
    ResolveSceneReferences(camera, nodes, models, materials, lights, tracks, skeletons, context);
    EnsureEnvironmentComponentNode(camera, nodes);
    return true;
}
} // namespace

bool SceneList::LoadScene(std::string filename, Assets::EnvironmentSetting& camera,
                          std::vector<std::shared_ptr<Assets::Node>>& nodes, std::vector<Assets::Model>& models,
                          std::vector<Assets::FMaterial>& materials,
                          std::vector<Assets::LightObject>& lights, std::vector<Assets::AnimationTrack>& tracks,
                          std::vector<Assets::Skeleton>& skeletons)
{
    return LoadSceneWithReferences(std::move(filename), camera, nodes, models, materials, lights, tracks, skeletons);
}

std::shared_ptr<Assets::Node> SceneList::AddSceneReferenceToScene(
    Assets::Scene& scene, const std::string& assetPath, const glm::vec3& translation)
{
    std::string normalizedPath;
    std::string error;
    if (!NormalizeReferenceAssetPath(assetPath, normalizedPath, error))
    {
        SPDLOG_WARN("Cannot add scene reference '{}': {}", assetPath, error);
        return nullptr;
    }

    const std::filesystem::path path(normalizedPath);
    if (!IsReferenceLeafOrHostExtension(path))
    {
        SPDLOG_WARN("Cannot add scene reference '{}': unsupported extension", assetPath);
        return nullptr;
    }

    std::string name = path.stem().string();
    if (name.empty())
    {
        name = "Scene Reference";
    }

    auto proxy = Assets::Node::CreateNode(name, translation, glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
                                          glm::vec3(1.0f), scene.GenerateInstanceId());
    auto sceneReference = std::make_shared<Runtime::SceneReferenceComponent>();
    sceneReference->SetAssetPath(normalizedPath);
    proxy->AddComponent(sceneReference);
    scene.AddNode(proxy);

    Assets::EnvironmentSetting ignoredCamera;
    std::vector<Assets::LightObject> ignoredLights;
    std::vector<Assets::AnimationTrack> ignoredTracks;
    std::vector<Assets::Skeleton> ignoredSkeletons;
    FSceneReferenceLoadContext context;
    std::vector<std::shared_ptr<Assets::Node>> resolvedNodes = scene.Nodes();
    const size_t originalNodeCount = resolvedNodes.size();
    ResolveSceneReferenceProxy(proxy, ignoredCamera, resolvedNodes, scene.Models(), scene.Materials(),
                               ignoredLights, ignoredTracks, ignoredSkeletons, context);
    scene.AddNodes(std::span<const std::shared_ptr<Assets::Node>>(
        resolvedNodes.data() + originalNodeCount, resolvedNodes.size() - originalNodeCount));
    scene.MarkDirty();
    return proxy;
}

}
