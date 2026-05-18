#include "Common/CoreMinimal.hpp"

#include <catch2/catch_test_macros.hpp>

#include "Application/Game/BrickPlayer/BrickPlayerSnapLogic.hpp"

namespace
{
    BrickPlayer::Shadow::FSnapConnector MakeCylinderConnector(BrickPlayer::Shadow::ESnapGender gender,
                                                              std::string group,
                                                              std::string profile,
                                                              float radius = 6.0f,
                                                              float length = 10.0f,
                                                              bool slide = false)
    {
        BrickPlayer::Shadow::FSnapConnector connector;
        connector.kind = BrickPlayer::Shadow::ESnapKind::Cylinder;
        connector.gender = gender;
        connector.group = std::move(group);
        connector.radius = radius;
        connector.length = length;
        connector.slide = slide;
        connector.sections.push_back({std::move(profile), radius, length});
        return connector;
    }
}

TEST_CASE("BrickPlayer snap logic accepts opposing compatible cylinder connectors", "[Unit][BrickPlayer][Snap]")
{
    const auto male = MakeCylinderConnector(BrickPlayer::Shadow::ESnapGender::Male, "stud", "R");
    const auto female = MakeCylinderConnector(BrickPlayer::Shadow::ESnapGender::Female, "Stud", "S");

    REQUIRE(BrickPlayer::Snap::AreConnectorsCompatible(male, female, 1.0f));
}

TEST_CASE("BrickPlayer snap logic rejects different connector groups", "[Unit][BrickPlayer][Snap]")
{
    const auto male = MakeCylinderConnector(BrickPlayer::Shadow::ESnapGender::Male, "stud", "R");
    const auto female = MakeCylinderConnector(BrickPlayer::Shadow::ESnapGender::Female, "axle", "R");

    REQUIRE_FALSE(BrickPlayer::Snap::AreConnectorsCompatible(male, female, 1.0f));
}

TEST_CASE("BrickPlayer snap hover filter matches nearby aligned target connectors", "[Unit][BrickPlayer][Snap]")
{
    const auto target = MakeCylinderConnector(BrickPlayer::Shadow::ESnapGender::Female,
                                              "stud",
                                              "R",
                                              0.004f,
                                              0.010f);

    const BrickPlayer::Snap::FHoverFilterResult nearHit =
        BrickPlayer::Snap::EvaluateHoverFilter(target,
                                               glm::vec3(0.01f, 0.02f, 0.01f),
                                               glm::vec3(0.0f, 1.0f, 0.0f),
                                               glm::vec3(0.0f),
                                               glm::vec3(0.0f, 1.0f, 0.0f),
                                               0.001f);
    REQUIRE(nearHit.passes);

    const BrickPlayer::Snap::FHoverFilterResult farHit =
        BrickPlayer::Snap::EvaluateHoverFilter(target,
                                               glm::vec3(0.10f, 0.0f, 0.0f),
                                               glm::vec3(0.0f, 1.0f, 0.0f),
                                               glm::vec3(0.0f),
                                               glm::vec3(0.0f, 1.0f, 0.0f),
                                               0.001f);
    REQUIRE_FALSE(farHit.passes);
}
