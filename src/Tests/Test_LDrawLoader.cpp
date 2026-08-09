#include <catch2/catch_all.hpp>

#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Data/Material.hpp"
#include "Modules/LDrawLoader/FLDrawLoader.h"
#include "Engine/Runtime/Components/RenderComponent.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>

namespace
{
    void CheckVec3Near(const glm::vec3& actual, const glm::vec3& expected)
    {
        CHECK(actual.x == Catch::Approx(expected.x).margin(1e-5f));
        CHECK(actual.y == Catch::Approx(expected.y).margin(1e-5f));
        CHECK(actual.z == Catch::Approx(expected.z).margin(1e-5f));
    }

    class ScopedLDrawSceneFile
    {
    public:
        explicit ScopedLDrawSceneFile(const std::string& contents)
        {
            auto uniqueSuffix = std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count());
            dir_ = std::filesystem::temp_directory_path() / "gkNextEngine" / "tests" / ("ldraw_loader_" + uniqueSuffix);
            std::filesystem::create_directories(dir_);
            path_ = dir_ / "scene.mpd";

            std::ofstream out(path_, std::ios::binary | std::ios::trunc);
            REQUIRE(out.is_open());
            out << contents;
        }

        ~ScopedLDrawSceneFile()
        {
            std::error_code ec;
            std::filesystem::remove_all(dir_, ec);
        }

        std::filesystem::path Path() const
        {
            return path_;
        }

    private:
        std::filesystem::path dir_;
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

    Assets::LDrawLoadOptions options;
    options.useLibraryPak = false;

    REQUIRE(Assets::FLDrawLoader::LoadLDrawScene(
        sceneFile.Path().string(),
        environment,
        nodes,
        models,
        materials,
        lights,
        tracks,
        skeletons,
        options));

    REQUIRE(nodes.size() == 2);

    std::shared_ptr<Assets::Node> parentNode;
    std::shared_ptr<Assets::Node> childNode;
    for (const auto& node : nodes)
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
    CHECK(parentRender->GetMaterials()[0] == childRender->GetMaterials()[0]);
}

TEST_CASE("LDraw loader applies configurable LDU scale to geometry and placement", "[Unit][LDraw]")
{
    ScopedLDrawSceneFile sceneFile(
        "0 FILE main.ldr\n"
        "1 16 10 20 30 1 0 0 0 1 0 0 0 1 brick.dat\n"
        "0 FILE brick.dat\n"
        "0 BFC CERTIFY CCW\n"
        "3 16 0 0 0 2 4 6 0 2 0\n");

    Assets::EnvironmentSetting environment;
    std::vector<std::shared_ptr<Assets::Node>> nodes;
    std::vector<Assets::Model> models;
    std::vector<Assets::FMaterial> materials;
    std::vector<Assets::LightObject> lights;
    std::vector<Assets::AnimationTrack> tracks;
    std::vector<Assets::Skeleton> skeletons;

    Assets::LDrawLoadOptions options;
    options.lduToWorldScale = 0.1f;
    options.useLibraryPak = false;

    REQUIRE(Assets::FLDrawLoader::LoadLDrawScene(
        sceneFile.Path().string(),
        environment,
        nodes,
        models,
        materials,
        lights,
        tracks,
        skeletons,
        options));

    REQUIRE(nodes.size() == 1);
    std::shared_ptr<Assets::Node> partNode = nodes[0];
    CheckVec3Near(partNode->Translation(), glm::vec3(-1.0f, -2.0f, 3.0f));

    auto renderComp = partNode->GetComponent<Runtime::RenderComponent>();
    REQUIRE(renderComp);
    REQUIRE(renderComp->GetModelId() < models.size());

    const auto& cpuVertices = models[renderComp->GetModelId()].CPUVertices();
    REQUIRE(cpuVertices.size() == 3);
    CheckVec3Near(cpuVertices[1].Position, glm::vec3(-0.2f, -0.4f, 0.6f));
    CheckVec3Near(cpuVertices[2].Position, glm::vec3(0.0f, -0.2f, 0.0f));
}
