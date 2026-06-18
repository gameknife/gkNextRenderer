#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "Modules/SplatLoader/FSplatQuant.hpp"
#include "Engine/Assets/Core/Node.h"
#include "Engine/Runtime/Components/GaussianSplatComponent.h"

#include <glm/gtc/quaternion.hpp>

TEST_CASE("SOG log position inverse", "[Unit][SOG]")
{
    REQUIRE(Assets::Sog::DecodeLogPosition(0.0f) == Catch::Approx(0.0f));
    REQUIRE(Assets::Sog::DecodeLogPosition(std::log(3.0f)) == Catch::Approx(2.0f));
    REQUIRE(Assets::Sog::DecodeLogPosition(-std::log(3.0f)) == Catch::Approx(-2.0f));
}

TEST_CASE("SOG smallest-three quaternion modes", "[Unit][SOG]")
{
    for (uint8_t mode = 0; mode < 4; ++mode)
    {
        const glm::quat quaternion = Assets::Sog::DecodeQuaternion(128, 128, 128, 252 + mode);
        REQUIRE(glm::length(quaternion) == Catch::Approx(1.0f).margin(1e-5f));
        const std::array<float, 4> components{quaternion.w, quaternion.x, quaternion.y, quaternion.z};
        REQUIRE(components[mode] > 0.99f);
    }
}

TEST_CASE("SOG scales are logarithmic", "[Unit][SOG]")
{
    const glm::mat3 covariance = Assets::Sog::BuildCovariance(
        glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(std::log(2.0f), std::log(3.0f), std::log(4.0f)));
    REQUIRE(covariance[0][0] == Catch::Approx(4.0f));
    REQUIRE(covariance[1][1] == Catch::Approx(9.0f));
    REQUIRE(covariance[2][2] == Catch::Approx(16.0f));
}

TEST_CASE("Gaussian splat component properties", "[Unit][SOG][GaussianSplatComponent]")
{
    auto node = Assets::Node::CreateNode("Splat", glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
                                         glm::vec3(1.0f), 7);
    auto component = std::make_shared<Runtime::GaussianSplatComponent>(3);
    node->AddComponent(component);

    const auto retrieved = node->GetComponent<Runtime::GaussianSplatComponent>();
    REQUIRE(retrieved != nullptr);
    CHECK(retrieved->GetSplatModelId() == 3);
    CHECK(retrieved->GetVisible());
    CHECK(retrieved->GetRayCastVisible());
    CHECK(retrieved->GetOpacityScale() == Catch::Approx(1.0f));

    retrieved->SetOpacityScale(-2.0f);
    CHECK(retrieved->GetOpacityScale() == Catch::Approx(0.0f));
    CHECK_FALSE(retrieved->ToggleVisible());
}
