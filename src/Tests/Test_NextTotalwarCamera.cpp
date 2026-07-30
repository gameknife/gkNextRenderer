#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Render/BattleCamera.h"

#include <glm/gtc/constants.hpp>

TEST_CASE("NextTotalwar camera WASD movement follows the current view heading",
          "[NextTotalwar][Camera]")
{
    NextTotalwar::FBattleCamera camera;
    camera.SetRotateLeft(true);
    camera.Tick(glm::half_pi<float>() / 0.9f, nullptr);
    camera.SetRotateLeft(false);

    const glm::vec3 beforeForward = camera.Focus();
    camera.SetMoveForward(true);
    camera.Tick(0.1f, nullptr);
    camera.SetMoveForward(false);
    CHECK(camera.Focus().x < beforeForward.x);
    CHECK_THAT(camera.Focus().z, Catch::Matchers::WithinAbs(beforeForward.z, 0.001f));

    const glm::vec3 beforeRight = camera.Focus();
    camera.SetMoveRight(true);
    camera.Tick(0.1f, nullptr);
    camera.SetMoveRight(false);
    CHECK(camera.Focus().z < beforeRight.z);
    CHECK_THAT(camera.Focus().x, Catch::Matchers::WithinAbs(beforeRight.x, 0.001f));
}

TEST_CASE("NextTotalwar camera middle-button pan uses screen-relative axes",
          "[NextTotalwar][Camera]")
{
    NextTotalwar::FBattleCamera camera;
    camera.PanByScreenDelta({100.0f, 0.0f});
    CHECK(camera.Focus().x < 0.0f);
    CHECK_THAT(camera.Focus().z, Catch::Matchers::WithinAbs(0.0f, 0.001f));

    camera.PanByScreenDelta({0.0f, 100.0f});
    CHECK(camera.Focus().z < 0.0f);
}

TEST_CASE("NextTotalwar camera follows a moving target and preserves manual offset",
          "[NextTotalwar][Camera]")
{
    NextTotalwar::FBattleCamera camera;
    camera.SetFollowTarget({50.0f, 0.0f, -20.0f}, true);
    REQUIRE(camera.IsFollowing());
    CHECK_THAT(camera.Focus().x, Catch::Matchers::WithinAbs(50.0f, 0.001f));
    CHECK_THAT(camera.Focus().z, Catch::Matchers::WithinAbs(-20.0f, 0.001f));

    camera.SetFollowTarget({60.0f, 0.0f, -10.0f}, false);
    camera.Tick(0.016f, nullptr);
    CHECK_THAT(camera.Focus().x, Catch::Matchers::WithinAbs(60.0f, 0.001f));
    CHECK_THAT(camera.Focus().z, Catch::Matchers::WithinAbs(-10.0f, 0.001f));

    camera.PanByScreenDelta({20.0f, 0.0f});
    camera.Tick(0.016f, nullptr);
    CHECK(camera.Focus().x < 60.0f);

    camera.ClearFollowTarget();
    CHECK_FALSE(camera.IsFollowing());
}
