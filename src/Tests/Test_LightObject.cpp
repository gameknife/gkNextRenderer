#include <catch2/catch_all.hpp>

#include "Engine/Assets/Loaders/FProcModel.hpp"
#include "Engine/Assets/Core/LightObject.hpp"
#include "Engine/Runtime/Components/LightComponent.hpp"

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

TEST_CASE("Light component owns procedural light properties", "[Unit][Assets][LightObject]")
{
    Assets::LightObject light{};
    light.p0 = glm::vec4(1.0f, 0.0f, 0.0f, 0.25f);
    light.lightType = Assets::LightTypePoint;
    Runtime::LightComponent component(light);
    component.SetMaterialIndex(42);

    const glm::mat4 transform =
        glm::translate(glm::mat4(1.0f), glm::vec3(3.0f, 4.0f, 5.0f)) *
        glm::scale(glm::mat4(1.0f), glm::vec3(2.0f));
    const Assets::LightObject transformed =
        Assets::LightObjects::Transform(component.Lights().front(), transform);

    CHECK(component.GetMaterialIndex() == 42);
    CHECK(component.GetLightType() == Assets::LightTypePoint);
    CHECK(glm::vec3(transformed.p0) == glm::vec3(5.0f, 4.0f, 5.0f));
    CHECK(transformed.p0.w == Catch::Approx(0.5f));
}

TEST_CASE("Area light transform preserves its declared normal orientation",
          "[Unit][Assets][LightObject]")
{
    Assets::LightObject light{};
    light.p0 = glm::vec4(-1.0f, 0.0f, -1.0f, 1.0f);
    light.p1 = glm::vec4(-1.0f, 0.0f, 1.0f, 1.0f);
    light.p3 = glm::vec4(1.0f, 0.0f, -1.0f, 1.0f);
    light.normal_area = glm::vec4(0.0f, -1.0f, 0.0f, 4.0f);
    light.lightType = Assets::LightTypeArea;

    const Assets::LightObject transformed = Assets::LightObjects::Transform(light, glm::mat4(1.0f));

    CHECK(glm::vec3(transformed.normal_area) == glm::vec3(0.0f, -1.0f, 0.0f));
    CHECK(transformed.normal_area.w == Catch::Approx(4.0f));
}
