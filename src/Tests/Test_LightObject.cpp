#include <catch2/catch_all.hpp>

#include "Engine/Assets/Loaders/FProcModel.hpp"

static_assert(sizeof(Assets::LightObject) == 80);
static_assert(alignof(Assets::LightObject) == 16);

TEST_CASE("Point light factory creates sphere proxy and analytic descriptor",
          "[Unit][Assets][LightObject]")
{
    const glm::vec3 position(1.5f, 2.25f, -3.0f);
    constexpr uint32_t materialIndex = 7;
    constexpr float radius = 0.2f;
    std::vector<Assets::LightObject> lights;

    const Assets::Model model = Assets::FProcModel::CreatePointLight(
        "TestPointLight", position, radius, materialIndex, lights);

    REQUIRE(lights.size() == 1);
    const Assets::LightObject& light = lights.front();
    CHECK(light.lightType == Assets::LightTypePoint);
    CHECK(light.lightMatIdx == materialIndex);
    CHECK(glm::vec3(light.p0) == position);
    CHECK(light.p0.w == Catch::Approx(radius));
    CHECK(light.normal_area == glm::vec4(0.0f));
    CHECK(model.Name() == "TestPointLight");
    CHECK_FALSE(model.CPUVertices().empty());
    CHECK_FALSE(model.CPUIndices().empty());
    CHECK(model.GetLocalAABBMin().x == Catch::Approx(position.x - radius));
    CHECK(model.GetLocalAABBMax().x == Catch::Approx(position.x + radius));
}
