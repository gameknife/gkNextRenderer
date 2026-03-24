#include <catch2/catch_all.hpp>

#include "Assets/Loaders/FLDrawGeometry.h"

namespace
{
    void CheckVec3Near(const glm::vec3& actual, const glm::vec3& expected)
    {
        CHECK(actual.x == Catch::Approx(expected.x).margin(1e-5f));
        CHECK(actual.y == Catch::Approx(expected.y).margin(1e-5f));
        CHECK(actual.z == Catch::Approx(expected.z).margin(1e-5f));
    }
}

TEST_CASE("LDraw geometry smooths normals across shallow shared edges", "[Unit][LDraw]")
{
    Assets::LDrawPartTemplate tmpl;
    tmpl.filename = "smooth.dat";

    Assets::LDrawFace faceA{};
    faceA.vertexCount = 3;
    faceA.colorCode = 16;
    faceA.vertices[0] = glm::vec3(0.0f, 0.0f, 0.0f);
    faceA.vertices[1] = glm::vec3(1.0f, 0.0f, 0.0f);
    faceA.vertices[2] = glm::vec3(0.0f, 1.0f, 0.0f);
    tmpl.faces.push_back(faceA);

    Assets::LDrawFace faceB{};
    faceB.vertexCount = 3;
    faceB.colorCode = 16;
    faceB.vertices[0] = glm::vec3(0.0f, 0.0f, 0.0f);
    faceB.vertices[1] = glm::vec3(1.0f, 0.0f, 0.0f);
    faceB.vertices[2] = glm::vec3(0.0f, 0.8660254f, 0.5f);
    tmpl.faces.push_back(faceB);

    const Assets::LDrawBuiltGeometry geometry = Assets::FLDrawGeometry::BuildPartGeometry(tmpl);
    // 2 input faces × 3 vertices = 6
    REQUIRE(geometry.vertices.size() == 6);

    const glm::vec3 expectedNormal = glm::normalize(glm::vec3(0.0f, 0.5f, 1.8660254f));
    // Face A: vertices 0,1,2; Face B: vertices 3,4,5
    CheckVec3Near(geometry.vertices[0].Normal, expectedNormal);
    CheckVec3Near(geometry.vertices[1].Normal, expectedNormal);
    CheckVec3Near(geometry.vertices[3].Normal, expectedNormal);
    CheckVec3Near(geometry.vertices[4].Normal, expectedNormal);
}

TEST_CASE("LDraw geometry preserves hard edges beyond smoothing threshold", "[Unit][LDraw]")
{
    Assets::LDrawPartTemplate tmpl;
    tmpl.filename = "hard_edge.dat";

    Assets::LDrawFace faceA{};
    faceA.vertexCount = 3;
    faceA.colorCode = 16;
    faceA.vertices[0] = glm::vec3(0.0f, 0.0f, 0.0f);
    faceA.vertices[1] = glm::vec3(1.0f, 0.0f, 0.0f);
    faceA.vertices[2] = glm::vec3(0.0f, 1.0f, 0.0f);
    tmpl.faces.push_back(faceA);

    Assets::LDrawFace faceB{};
    faceB.vertexCount = 3;
    faceB.colorCode = 16;
    faceB.vertices[0] = glm::vec3(0.0f, 0.0f, 0.0f);
    faceB.vertices[1] = glm::vec3(1.0f, 0.0f, 0.0f);
    faceB.vertices[2] = glm::vec3(0.0f, 0.0f, 1.0f);
    tmpl.faces.push_back(faceB);

    const Assets::LDrawBuiltGeometry geometry = Assets::FLDrawGeometry::BuildPartGeometry(tmpl);
    // 2 input faces × 3 vertices = 6
    REQUIRE(geometry.vertices.size() == 6);

    // Face A: vertices 0,1,2; Face B: vertices 3,4,5
    CheckVec3Near(geometry.vertices[0].Normal, glm::vec3(0.0f, 0.0f, 1.0f));
    CheckVec3Near(geometry.vertices[1].Normal, glm::vec3(0.0f, 0.0f, 1.0f));
    CheckVec3Near(geometry.vertices[3].Normal, glm::vec3(0.0f, 1.0f, 0.0f));
    CheckVec3Near(geometry.vertices[4].Normal, glm::vec3(0.0f, 1.0f, 0.0f));
}

TEST_CASE("LDraw geometry applies configurable LDU scale", "[Unit][LDraw]")
{
    Assets::LDrawPartTemplate tmpl;
    tmpl.filename = "scaled.dat";

    Assets::LDrawFace face{};
    face.vertexCount = 3;
    face.colorCode = 16;
    face.vertices[0] = glm::vec3(0.0f, 0.0f, 0.0f);
    face.vertices[1] = glm::vec3(2.0f, 4.0f, 6.0f);
    face.vertices[2] = glm::vec3(0.0f, 2.0f, 0.0f);
    tmpl.faces.push_back(face);

    Assets::LDrawLoadOptions options;
    options.lduToWorldScale = 0.5f;

    const Assets::LDrawBuiltGeometry geometry = Assets::FLDrawGeometry::BuildPartGeometry(tmpl, options);
    // 1 input face × 3 vertices = 3
    REQUIRE(geometry.vertices.size() == 3);

    // Face vertices (indices 0,1,2)
    CheckVec3Near(geometry.vertices[0].Position, glm::vec3(0.0f, 0.0f, 0.0f));
    CheckVec3Near(geometry.vertices[1].Position, glm::vec3(-1.0f, -2.0f, 3.0f));
    CheckVec3Near(geometry.vertices[2].Position, glm::vec3(0.0f, -1.0f, 0.0f));
}
