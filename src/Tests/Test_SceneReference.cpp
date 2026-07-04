#include <catch2/catch_all.hpp>

#include "Engine/Assets/Core/GaussianSplat.hpp"
#include "Engine/Assets/Core/Model.hpp"
#include "Engine/Assets/Core/Node.h"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Assets/Data/Material.hpp"
#include "Engine/Assets/Data/Skeleton.hpp"
#include "Engine/Runtime/Components/EnvironmentComponent.h"
#include "Engine/Runtime/Components/SceneReferenceComponent.h"
#include "Engine/Runtime/Scene/SceneList.hpp"
#include "Engine/Utilities/FileHelper.hpp"

#include <fmt/format.h>

#include <filesystem>
#include <fstream>
#include <memory>

namespace
{
    constexpr const char* kSceneReferenceTestDir = "assets/test_scene_reference";

    void WriteTextAsset(const std::string& relativePath, const std::string& text)
    {
        const std::filesystem::path absolutePath =
            Utilities::FileHelper::GetPlatformFilePath(relativePath.c_str());
        std::filesystem::create_directories(absolutePath.parent_path());

        std::ofstream out(absolutePath, std::ios::binary);
        REQUIRE(out.good());
        out << text;
    }

    std::string EmptySceneGltf(const std::string& nodeName)
    {
        return fmt::format(R"({{
  "asset": {{ "version": "2.0" }},
  "scenes": [{{ "nodes": [0] }}],
  "scene": 0,
  "nodes": [{{ "name": "{}" }}]
}})",
                           nodeName);
    }

    std::string HostWithObjectReference(const std::string& assetPath)
    {
        return fmt::format(R"({{
  "asset": {{ "version": "2.0" }},
  "scenes": [{{ "nodes": [0] }}],
  "scene": 0,
  "nodes": [{{
    "name": "Proxy",
    "extras": {{
      "gkSceneReference": {{ "version": 1, "asset": "{}" }}
    }}
  }}]
}})",
                           assetPath);
    }

    std::string HostWithStringReference(const std::string& assetPath)
    {
        return fmt::format(R"({{
  "asset": {{ "version": "2.0" }},
  "scenes": [{{ "nodes": [0] }}],
  "scene": 0,
  "nodes": [{{
    "name": "Proxy",
    "extras": {{ "gkSceneReference": "{}" }}
  }}]
}})",
                           assetPath);
    }

    bool LoadSceneForTest(const std::string& path, std::vector<std::shared_ptr<Assets::Node>>& nodes)
    {
        Assets::EnvironmentSetting camera;
        std::vector<Assets::Model> models;
        std::vector<Assets::FMaterial> materials;
        std::vector<Assets::LightObject> lights;
        std::vector<Assets::AnimationTrack> tracks;
        std::vector<Assets::Skeleton> skeletons;
        std::vector<Assets::FGaussianSplatData> splats;
        return Runtime::Scene::SceneList::LoadScene(path, camera, nodes, models, materials, lights,
                                                    tracks, skeletons, &splats);
    }

    Runtime::EnvironmentComponent* FindEnvironmentComponent(
        const std::vector<std::shared_ptr<Assets::Node>>& nodes)
    {
        for (const auto& node : nodes)
        {
            if (node)
            {
                if (auto* environment = node->GetComponentPtr<Runtime::EnvironmentComponent>())
                {
                    return environment;
                }
            }
        }
        return nullptr;
    }
}

TEST_CASE("Scene references load object schema as proxy plus internal nodes", "[Unit][SceneReference]")
{
    Utilities::Package::FPackageFileSystem pakSys(Utilities::Package::EPM_OsFile);
    const std::string leafPath = std::string(kSceneReferenceTestDir) + "/leaf_object.gltf";
    const std::string hostPath = std::string(kSceneReferenceTestDir) + "/host_object.gltf";
    WriteTextAsset(leafPath, EmptySceneGltf("Leaf"));
    WriteTextAsset(hostPath, HostWithObjectReference(leafPath));

    std::vector<std::shared_ptr<Assets::Node>> nodes;
    REQUIRE(LoadSceneForTest(hostPath, nodes));
    REQUIRE(nodes.size() == 3);
    REQUIRE(FindEnvironmentComponent(nodes) != nullptr);

    auto reference = nodes[0]->GetComponent<Runtime::SceneReferenceComponent>();
    REQUIRE(reference != nullptr);
    CHECK(reference->GetStatus() == Runtime::ESceneReferenceStatus::Loaded);
    CHECK(reference->GetLoadedNodeCount() == 1);
    CHECK(nodes[1]->IsSceneReferenceInternal());
    CHECK(nodes[1]->GetSceneReferenceOwnerProxyId() == nodes[0]->GetInstanceId());
    REQUIRE(nodes[1]->GetParent() != nullptr);
    CHECK(nodes[1]->GetParent()->GetInstanceId() == nodes[0]->GetInstanceId());
}

TEST_CASE("Scene references load string schema", "[Unit][SceneReference]")
{
    Utilities::Package::FPackageFileSystem pakSys(Utilities::Package::EPM_OsFile);
    const std::string leafPath = std::string(kSceneReferenceTestDir) + "/leaf_string.gltf";
    const std::string hostPath = std::string(kSceneReferenceTestDir) + "/host_string.gltf";
    WriteTextAsset(leafPath, EmptySceneGltf("Leaf"));
    WriteTextAsset(hostPath, HostWithStringReference(leafPath));

    std::vector<std::shared_ptr<Assets::Node>> nodes;
    REQUIRE(LoadSceneForTest(hostPath, nodes));
    REQUIRE(nodes.size() == 3);
    REQUIRE(FindEnvironmentComponent(nodes) != nullptr);

    auto reference = nodes[0]->GetComponent<Runtime::SceneReferenceComponent>();
    REQUIRE(reference != nullptr);
    CHECK(reference->GetStatus() == Runtime::ESceneReferenceStatus::Loaded);
}

TEST_CASE("Scene references detect direct self cycle", "[Unit][SceneReference]")
{
    Utilities::Package::FPackageFileSystem pakSys(Utilities::Package::EPM_OsFile);
    const std::string hostPath = std::string(kSceneReferenceTestDir) + "/self_cycle.gltf";
    WriteTextAsset(hostPath, HostWithObjectReference(hostPath));

    std::vector<std::shared_ptr<Assets::Node>> nodes;
    REQUIRE(LoadSceneForTest(hostPath, nodes));
    REQUIRE(nodes.size() == 2);
    REQUIRE(FindEnvironmentComponent(nodes) != nullptr);

    auto reference = nodes[0]->GetComponent<Runtime::SceneReferenceComponent>();
    REQUIRE(reference != nullptr);
    CHECK(reference->GetStatus() == Runtime::ESceneReferenceStatus::CycleDetected);
}
