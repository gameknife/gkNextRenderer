#include <catch2/catch_all.hpp>

#include "Assets/Core/Node.h"
#include "Assets/Data/Material.hpp"
#include "Assets/Loaders/FLDrawLoader.h"
#include "Runtime/Components/RenderComponent.h"

#include <chrono>
#include <filesystem>
#include <fstream>

namespace
{
    class ScopedLDrawSceneFile
    {
    public:
        explicit ScopedLDrawSceneFile(const std::string& contents)
        {
            auto uniqueSuffix = std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count());
            path_ = std::filesystem::current_path() / ("ldraw_loader_" + uniqueSuffix + ".mpd");

            std::ofstream out(path_);
            REQUIRE(out.is_open());
            out << contents;
        }

        ~ScopedLDrawSceneFile()
        {
            std::error_code ec;
            std::filesystem::remove(path_, ec);
        }

        std::filesystem::path Path() const
        {
            return path_;
        }

    private:
        std::filesystem::path path_;
    };
}

TEST_CASE("LDraw loader preserves MPD submodel hierarchy as nodes", "[Unit][LDraw]")
{
    ScopedLDrawSceneFile sceneFile(
        "0 FILE main.ldr\n"
        "1 2 0 0 0 1 0 0 0 1 0 0 0 1 submodel.ldr\n"
        "0 FILE submodel.ldr\n"
        "0 BFC CERTIFY CCW\n"
        "3 16 0 0 0 10 0 0 0 10 0\n"
        "1 16 20 0 0 1 0 0 0 1 0 0 0 1 leaf.dat\n"
        "0 FILE leaf.dat\n"
        "0 BFC CERTIFY CCW\n"
        "3 16 0 0 0 5 0 0 0 5 0\n");

    Assets::EnvironmentSetting environment;
    std::vector<std::shared_ptr<Assets::Node>> nodes;
    std::vector<Assets::Model> models;
    std::vector<Assets::FMaterial> materials;
    std::vector<Assets::LightObject> lights;
    std::vector<Assets::AnimationTrack> tracks;
    std::vector<Assets::Skeleton> skeletons;

    REQUIRE(Assets::FLDrawLoader::LoadLDrawScene(
        sceneFile.Path().string(),
        environment,
        nodes,
        models,
        materials,
        lights,
        tracks,
        skeletons));

    std::vector<std::shared_ptr<Assets::Node>> sceneNodes;
    for (const auto& node : nodes)
    {
        if (node->GetName() != "ldraw_floor")
            sceneNodes.push_back(node);
    }

    REQUIRE(sceneNodes.size() == 2);

    std::shared_ptr<Assets::Node> parentNode;
    std::shared_ptr<Assets::Node> childNode;
    for (const auto& node : sceneNodes)
    {
        if (node->GetParent() == nullptr)
            parentNode = node;
        else
            childNode = node;
    }

    REQUIRE(parentNode);
    REQUIRE(childNode);
    CHECK(parentNode->Children().size() == 1);
    CHECK(childNode->GetParent() == parentNode.get());
    CHECK(parentNode->GetName().find("submodel.ldr") != std::string::npos);
    CHECK(childNode->GetName().find("leaf.dat") != std::string::npos);

    auto parentRender = parentNode->GetComponent<Runtime::RenderComponent>();
    auto childRender = childNode->GetComponent<Runtime::RenderComponent>();
    REQUIRE(parentRender);
    REQUIRE(childRender);
    CHECK(parentRender->IsDrawable());
    CHECK(childRender->IsDrawable());

    REQUIRE(parentRender->GetModelId() < models.size());
    REQUIRE(childRender->GetModelId() < models.size());
    CHECK(models[parentRender->GetModelId()].NumberOfVertices() == 3);
    CHECK(models[childRender->GetModelId()].NumberOfVertices() == 3);
    CHECK(parentRender->Materials()[0] == childRender->Materials()[0]);
}
