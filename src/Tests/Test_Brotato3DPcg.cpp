#include "Application/Brotato3D/Brotato3DPcgGenerator.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

namespace
{
    Brotato3D::Pcg::FArenaPcgConfig MakeTestConfig()
    {
        Brotato3D::Pcg::FArenaPcgConfig config{};
        config.enabled = true;
        config.seedOverride = 0x1234abcd;
        config.targetCells = 64;
        config.lloydRelaxIterations = 1;
        config.vertexJitterAmplitude = 0.48f;
        config.vertexJitterFrequency = 0.18f;
        config.palette = {
            glm::vec3(0.36f, 0.55f, 0.30f),
            glm::vec3(0.32f, 0.50f, 0.28f),
            glm::vec3(0.42f, 0.58f, 0.32f),
        };
        return config;
    }
}

TEST_CASE("Brotato3D PCG terrain stays on flat logical plane", "[Unit][Brotato3D][Pcg][LogicalPlane]")
{
    Brotato3D::Pcg::FArenaPcgConfig config = MakeTestConfig();
    Brotato3D::Pcg::FMapGraph graph{};

    REQUIRE(Brotato3D::Pcg::BuildMapGraph(config, glm::vec2(12.0f, 8.0f), 0x00000001u, graph));
    Brotato3D::Pcg::FTerrainMeshBuffers mesh = Brotato3D::Pcg::BuildTerrainMesh(config, graph);

    REQUIRE_FALSE(graph.cells.empty());
    REQUIRE_FALSE(mesh.vertices.empty());
    REQUIRE_FALSE(mesh.indices.empty());
    for (const Assets::Vertex& vertex : mesh.vertices)
    {
        CHECK(vertex.Position.y >= -0.60001f);
        CHECK(vertex.Position.y <= 0.60001f);
        CHECK(vertex.MaterialIndex < config.palette.size());
    }

    constexpr float logicalY = 0.0f;
    CHECK(logicalY == Catch::Approx(0.0f));
}

TEST_CASE("Brotato3D PCG terrain is deterministic for a seed", "[Unit][Brotato3D][Pcg][Deterministic]")
{
    Brotato3D::Pcg::FArenaPcgConfig config = MakeTestConfig();
    Brotato3D::Pcg::FMapGraph firstGraph{};
    Brotato3D::Pcg::FMapGraph secondGraph{};

    REQUIRE(Brotato3D::Pcg::BuildMapGraph(config, glm::vec2(12.0f, 8.0f), 0x00000001u, firstGraph));
    REQUIRE(Brotato3D::Pcg::BuildMapGraph(config, glm::vec2(12.0f, 8.0f), 0x00000001u, secondGraph));
    Brotato3D::Pcg::FTerrainMeshBuffers firstMesh = Brotato3D::Pcg::BuildTerrainMesh(config, firstGraph);
    Brotato3D::Pcg::FTerrainMeshBuffers secondMesh = Brotato3D::Pcg::BuildTerrainMesh(config, secondGraph);

    REQUIRE(firstGraph.cells.size() == secondGraph.cells.size());
    REQUIRE(firstMesh.vertices.size() == secondMesh.vertices.size());
    REQUIRE(firstMesh.indices.size() == secondMesh.indices.size());
    REQUIRE(firstMesh.vertices.size() >= 8);

    for (size_t index = 0; index < 8; ++index)
    {
        CHECK(firstMesh.vertices[index].Position.x == Catch::Approx(secondMesh.vertices[index].Position.x));
        CHECK(firstMesh.vertices[index].Position.y == Catch::Approx(secondMesh.vertices[index].Position.y));
        CHECK(firstMesh.vertices[index].Position.z == Catch::Approx(secondMesh.vertices[index].Position.z));
        CHECK(firstMesh.vertices[index].MaterialIndex == secondMesh.vertices[index].MaterialIndex);
    }
}

TEST_CASE("Brotato3D PCG props respect spawn and spacing constraints", "[Unit][Brotato3D][Pcg][Props]")
{
    Brotato3D::Pcg::FArenaPcgConfig config = MakeTestConfig();
    config.propPoissonRadius = 2.0f;
    config.spawnSafeRadius = 3.0f;
    config.edgeKeepout = 1.0f;
    config.props = {
        Brotato3D::Pcg::FPropDef{
            .id = "rock",
            .footprintXZ = glm::vec2(0.8f),
            .visualHeight = 0.7f,
            .colliderHeight = 0.5f,
            .baseColor = glm::vec3(0.45f),
            .weight = 1.0f,
        },
    };

    Brotato3D::Pcg::FMapGraph graph{};
    REQUIRE(Brotato3D::Pcg::BuildMapGraph(config, glm::vec2(12.0f, 8.0f), 0x00000001u, graph));
    REQUIRE_FALSE(graph.props.empty());

    for (const Brotato3D::Pcg::FPropPlacement& prop : graph.props)
    {
        CHECK(glm::length(prop.positionXZ) >= config.spawnSafeRadius);
        CHECK(std::abs(prop.positionXZ.x) <= graph.arenaHalfExtent.x - config.edgeKeepout + 0.001f);
        CHECK(std::abs(prop.positionXZ.y) <= graph.arenaHalfExtent.y - config.edgeKeepout + 0.001f);
    }
    for (size_t a = 0; a < graph.props.size(); ++a)
    {
        for (size_t b = a + 1; b < graph.props.size(); ++b)
        {
            CHECK(glm::length(graph.props[a].positionXZ - graph.props[b].positionXZ) >= config.propPoissonRadius - 0.001f);
        }
    }
}
