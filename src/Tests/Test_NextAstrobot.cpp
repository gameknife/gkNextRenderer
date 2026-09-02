#include <catch2/catch_all.hpp>

#include <algorithm>
#include <string>
#include <vector>

#include "Engine/Assets/Data/RigAsset.hpp"
#include "Modules/ScadLoader/FScadRig.h"

#include "Application/Game/NextAstrobot/Level/LevelFlow.hpp"
#include "Application/Game/NextAstrobot/Mechanisms/MechanismCurves.hpp"

// ============================================================================
// Test_NextAstrobot.cpp - The parts of NextAstrobot that can be pinned down
// without an engine: the mechanism motion curves, the level flow state machine,
// and the contract the player rig asset owes the animation code.
// ============================================================================

using namespace NextAstrobot;

TEST_CASE("Astro mechanism curves are periodic and bounded", "[Unit][NextAstrobot]")
{
    SECTION("PingPong01 runs 0 -> 1 -> 0 and stays in range")
    {
        constexpr float period = 4.0f;
        CHECK(PingPong01(0.0f, period) == Catch::Approx(0.0f).margin(1e-5));
        CHECK(PingPong01(period * 0.5f, period) == Catch::Approx(1.0f).margin(1e-5));
        CHECK(PingPong01(period, period) == Catch::Approx(0.0f).margin(1e-5));
        // Same value one period later: a rail platform must not drift.
        CHECK(PingPong01(1.3f, period) == Catch::Approx(PingPong01(1.3f + period, period)).margin(1e-5));
        for (float t = -6.0f; t < 12.0f; t += 0.05f)
        {
            const float value = PingPong01(t, period);
            REQUIRE(value >= 0.0f);
            REQUIRE(value <= 1.0f);
        }
        // Eased ends: the reversal is slower than the middle of the sweep.
        const float nearEnd = PingPong01(period * 0.5f, period) - PingPong01(period * 0.5f - 0.1f, period);
        const float midSweep = PingPong01(period * 0.25f, period) - PingPong01(period * 0.25f - 0.1f, period);
        CHECK(nearEnd < midSweep);
        // A degenerate period parks the mechanism instead of dividing by zero.
        CHECK(PingPong01(3.0f, 0.0f) == Catch::Approx(0.0f));
    }

    SECTION("Swing is a sine of the requested amplitude")
    {
        constexpr float period = 3.2f;
        constexpr float amp = 20.0f;
        CHECK(Swing(0.0f, period, 0.0f, amp) == Catch::Approx(0.0f).margin(1e-4));
        CHECK(Swing(period * 0.25f, period, 0.0f, amp) == Catch::Approx(amp).margin(1e-3));
        CHECK(Swing(period * 0.75f, period, 0.0f, amp) == Catch::Approx(-amp).margin(1e-3));
        CHECK(Swing(1.1f, period, 0.0f, amp) == Catch::Approx(Swing(1.1f + period, period, 0.0f, amp)).margin(1e-3));
        // A phase of 0.25 is a quarter period ahead.
        CHECK(Swing(0.0f, period, 0.25f, amp) == Catch::Approx(Swing(period * 0.25f, period, 0.0f, amp)).margin(1e-3));
        CHECK(Swing(1.0f, 0.0f, 0.0f, amp) == Catch::Approx(0.0f));
    }

    SECTION("Approach converges without overshooting")
    {
        CHECK(Approach(0.0f, 10.0f, 25.0f, 1.0f) == Catch::Approx(10.0f));  // step exceeds the gap
        CHECK(Approach(0.0f, 10.0f, 4.0f, 1.0f) == Catch::Approx(4.0f));
        CHECK(Approach(10.0f, 0.0f, 4.0f, 1.0f) == Catch::Approx(6.0f));
        CHECK(Approach(5.0f, 5.0f, 100.0f, 1.0f) == Catch::Approx(5.0f));
        // Negative rates and negative dt must not push the value away from the target.
        CHECK(Approach(0.0f, 10.0f, -4.0f, 1.0f) == Catch::Approx(4.0f));
        CHECK(Approach(3.0f, 10.0f, 4.0f, -1.0f) == Catch::Approx(3.0f));

        float value = -12.0f;
        for (int i = 0; i < 200; ++i)
        {
            value = Approach(value, 12.0f, 25.0f, 1.0f / 60.0f);
        }
        CHECK(value == Catch::Approx(12.0f));
    }

    SECTION("Damp is frame-rate independent")
    {
        const float once = Damp(0.0f, 1.0f, 8.0f, 0.1f);
        float twice = Damp(0.0f, 1.0f, 8.0f, 0.05f);
        twice = Damp(twice, 1.0f, 8.0f, 0.05f);
        CHECK(once == Catch::Approx(twice).margin(1e-5));
        CHECK(Damp(0.0f, 1.0f, 0.0f, 0.1f) == Catch::Approx(1.0f));
    }
}

TEST_CASE("Astro level flow walks title -> playing -> goal -> result", "[Unit][NextAstrobot]")
{
    FLevelFlow flow;
    flow.Reset(true, 4.0f, 0.8f, 2.0f);
    CHECK(flow.State() == ELevelState::Title);
    CHECK_FALSE(flow.WorldRunning());

    // A key press skips into the intro fly-through, a second one into play.
    CHECK(flow.RequestSkip());
    CHECK(flow.State() == ELevelState::Intro);
    flow.Update(1.0);
    CHECK(flow.IntroProgress01() == Catch::Approx(0.25f).margin(1e-4));
    CHECK(flow.RequestSkip());
    CHECK(flow.State() == ELevelState::Playing);
    CHECK_FALSE(flow.RequestSkip()); // nothing left to skip
    CHECK(flow.WorldRunning());

    flow.Update(1.5);
    CHECK(flow.Stats().elapsedSeconds == Catch::Approx(1.5));

    // Pausing suspends the world and restores the previous state.
    flow.RequestPause(true);
    CHECK(flow.State() == ELevelState::Paused);
    CHECK_FALSE(flow.WorldRunning());
    flow.Update(2.0);
    CHECK(flow.Stats().elapsedSeconds == Catch::Approx(1.5)); // the clock does not run while paused
    flow.RequestPause(false);
    CHECK(flow.State() == ELevelState::Playing);

    flow.NotifyGoalReached();
    CHECK(flow.State() == ELevelState::Goal);
    flow.Update(1.0);
    CHECK(flow.State() == ELevelState::Goal);
    flow.Update(1.5);
    CHECK(flow.State() == ELevelState::Result);
    // The result screen is terminal until the level is reloaded.
    flow.Update(10.0);
    CHECK(flow.State() == ELevelState::Result);
    CHECK(flow.Stats().deaths == 0);
}

TEST_CASE("Astro level flow counts deaths and holds until the respawn lands", "[Unit][NextAstrobot]")
{
    FLevelFlow flow;
    flow.Reset(false, 4.0f, 0.8f, 2.0f);
    CHECK(flow.RequestSkip());
    CHECK(flow.State() == ELevelState::Playing); // no intro path: title goes straight to play

    flow.NotifyDeath();
    CHECK(flow.State() == ELevelState::Dead);
    CHECK(flow.Stats().deaths == 1);
    CHECK(flow.WorldRunning()); // the body keeps falling behind the fade
    CHECK(flow.DeathFade01() == Catch::Approx(0.0f));
    flow.Update(0.4);
    CHECK(flow.DeathFade01() == Catch::Approx(1.0f));
    // Dying again while already dead must not double-count.
    flow.NotifyDeath();
    CHECK(flow.Stats().deaths == 1);
    // Nothing but the respawn leaves the Dead state.
    flow.Update(5.0);
    CHECK(flow.State() == ELevelState::Dead);
    flow.NotifyRespawned();
    CHECK(flow.State() == ELevelState::Playing);
    CHECK(flow.DeathFade01() == Catch::Approx(0.0f));

    // Goal only fires from Playing, so a death cannot be converted into a win.
    flow.NotifyDeath();
    flow.NotifyGoalReached();
    CHECK(flow.State() == ELevelState::Dead);
}

TEST_CASE("astro_bot rig meets the NextAstrobot animation contract", "[Unit][ScadRig][AstroBotRig]")
{
    Assets::FRigAsset rig;
    std::string error;
    std::vector<std::string> warnings;
    REQUIRE(Assets::FScadRigLoader::LoadRig("assets/scad/characters/astro_bot.scad", {}, rig, error, &warnings));
    INFO(error);
    for (const std::string& warning : warnings)
    {
        INFO(warning);
    }
    CHECK(warnings.empty());

    for (const char* bone : {"bone_root", "bone_torso", "bone_head", "bone_arm_l", "bone_arm_r", "bone_leg_l",
                             "bone_leg_r", "bone_jet"})
    {
        INFO(bone);
        REQUIRE(rig.FindBone(bone) >= 0);
    }
    CHECK(rig.bones.size() == 8);
    CHECK(rig.bones[0].name == "bone_root");
    // The root is the ground anchor: the character stands on its own origin.
    CHECK(rig.bones[0].bindT.y == Catch::Approx(0.0f).margin(1e-4));

    // PlayerRigVisual selects among exactly these; a missing one silently falls back to
    // the bind pose, which reads as a frozen character rather than an error.
    for (const char* clip : {"idle", "run", "jump", "fall", "hover", "land", "punch", "hurt", "win", "zip", "wave"})
    {
        INFO(clip);
        REQUIRE(rig.FindClip(clip) != nullptr);
    }
    for (const char* oneShot : {"jump", "land", "punch", "hurt"})
    {
        INFO(oneShot);
        CHECK_FALSE(rig.FindClip(oneShot)->loop);
    }
    for (const char* looping : {"idle", "run", "fall", "hover", "win", "zip", "wave"})
    {
        INFO(looping);
        CHECK(rig.FindClip(looping)->loop);
    }
    // The run cycle is authored for the config's 6 m/s so PlayerRigVisual can scale it.
    CHECK(rig.FindClip("run")->duration == Catch::Approx(0.5f).margin(1e-4));

    // The character is 1.6 m tall, which is what every jump height and platform gap in
    // kit_astro was laid out against.
    float highestY = 0.0f;
    for (const Assets::Model& model : rig.partModels)
    {
        for (const Assets::Vertex& vertex : model.CPUVertices())
        {
            highestY = std::max(highestY, vertex.Position.y);
        }
    }
    // Part vertices are bone-local; add the head bone's world height to reach the crown.
    const int32_t head = rig.FindBone("bone_head");
    REQUIRE(head >= 0);
    float headHeight = 0.0f;
    for (int32_t bone = head; bone >= 0; bone = rig.bones[bone].parent)
    {
        headHeight += rig.bones[bone].bindT.y;
    }
    CHECK(headHeight > 1.15f);
    CHECK(headHeight < 1.35f);
}
