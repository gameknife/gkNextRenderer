#include <catch2/catch_all.hpp>

#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Data/RigAsset.hpp"
#include "Gameplay/Rig/RigInstance.h"
#include "Gameplay/Rig/RigLayeredAnimator.h"

#include <memory>
#include <vector>

using namespace Assets;
using NextGameplay::FRigAnimator;

namespace
{
    // Two bones: root at origin, arm bound at (0, 1, 0). Clips:
    //   "move": loop, root rises 0..1 over 1s.
    //   "pose": non-loop single key, root at y = 0.5.
    FRigAsset MakeTestAsset()
    {
        FRigAsset asset;

        FRigBone root;
        root.name = "bone_root";
        root.children = {1};
        asset.bones.push_back(root);

        FRigBone arm;
        arm.name = "bone_arm";
        arm.parent = 0;
        arm.bindT = glm::vec3(0.0f, 1.0f, 0.0f);
        asset.bones.push_back(arm);

        FRigClip move;
        move.name = "move";
        move.duration = 1.0f;
        FRigChannel moveRoot;
        moveRoot.bone = 0;
        moveRoot.position.Keys = {{0.0f, glm::vec3(0.0f)}, {1.0f, glm::vec3(0.0f, 1.0f, 0.0f)}};
        move.channels.push_back(moveRoot);
        asset.clips.push_back(move);

        FRigClip pose;
        pose.name = "pose";
        pose.loop = false;
        FRigChannel poseRoot;
        poseRoot.bone = 0;
        poseRoot.position.Keys = {{0.0f, glm::vec3(0.0f, 0.5f, 0.0f)}};
        pose.channels.push_back(poseRoot);
        asset.clips.push_back(pose);

        return asset;
    }

    struct TestRig
    {
        FRigAsset asset = MakeTestAsset();
        std::shared_ptr<Node> root;
        std::shared_ptr<Node> arm;
        FRigAnimator animator;

        TestRig()
        {
            root = Node::CreateNode("bone_root", glm::vec3(0.0f), glm::quat(1, 0, 0, 0), glm::vec3(1.0f), 1);
            arm = Node::CreateNode("bone_arm", glm::vec3(0.0f, 1.0f, 0.0f), glm::quat(1, 0, 0, 0), glm::vec3(1.0f), 2);
            arm->SetParent(root);
            animator.Bind(&asset, {root.get(), arm.get()}, root.get());
        }
    };

    FRigAsset MakeLayeredAsset()
    {
        FRigAsset asset;

        FRigBone root;
        root.name = "bone_root";
        root.children = {1, 2};
        asset.bones.push_back(root);

        FRigBone arm;
        arm.name = "bone_arm";
        arm.parent = 0;
        arm.bindT = glm::vec3(0.0f, 1.0f, 0.0f);
        asset.bones.push_back(arm);

        FRigBone leg;
        leg.name = "bone_leg";
        leg.parent = 0;
        leg.bindT = glm::vec3(0.0f, -1.0f, 0.0f);
        asset.bones.push_back(leg);

        auto addRootMove = [&asset](std::string name, float end)
        {
            FRigClip clip;
            clip.name = std::move(name);
            clip.duration = 1.0f;
            FRigChannel channel;
            channel.bone = 0;
            channel.position.Keys = {{0.0f, glm::vec3(0.0f)},
                                     {1.0f, glm::vec3(0.0f, end, 0.0f)}};
            clip.channels.push_back(channel);
            asset.clips.push_back(std::move(clip));
        };
        addRootMove("move_a", 1.0f);
        addRootMove("move_b", 2.0f);

        FRigClip base;
        base.name = "base";
        FRigChannel baseArm;
        baseArm.bone = 1;
        baseArm.position.Keys = {{0.0f, glm::vec3(0.2f, 0.0f, 0.0f)}};
        base.channels.push_back(baseArm);
        FRigChannel baseLeg;
        baseLeg.bone = 2;
        baseLeg.rotation.Keys = {{0.0f, glm::angleAxis(glm::radians(30.0f), glm::vec3(1.0f, 0.0f, 0.0f))}};
        base.channels.push_back(baseLeg);
        asset.clips.push_back(base);

        auto addArmPose = [&asset](std::string name, const glm::quat& rotation, float x)
        {
            FRigClip clip;
            clip.name = std::move(name);
            FRigChannel channel;
            channel.bone = 1;
            channel.position.Keys = {{0.0f, glm::vec3(x, 0.0f, 0.0f)}};
            channel.rotation.Keys = {{0.0f, rotation}};
            clip.channels.push_back(channel);
            asset.clips.push_back(std::move(clip));
        };
        const glm::quat aimRotation =
            glm::angleAxis(glm::radians(45.0f), glm::vec3(0.0f, 0.0f, 1.0f));
        FRigClip aim;
        aim.name = "aim";
        FRigChannel aimArm;
        aimArm.bone = 1;
        aimArm.rotation.Keys = {{0.0f, aimRotation}};
        aim.channels.push_back(aimArm);
        asset.clips.push_back(aim);
        addArmPose("aim_positive", aimRotation, 1.0f);
        addArmPose("aim_negative", -aimRotation, -1.0f);

        FRigClip recoil;
        recoil.name = "recoil";
        recoil.duration = 0.2f;
        recoil.loop = false;
        FRigChannel recoilArm;
        recoilArm.bone = 1;
        recoilArm.rotation.Keys = {
            {0.0f, glm::quat(1.0f, 0.0f, 0.0f, 0.0f)},
            {0.1f, glm::angleAxis(glm::radians(20.0f), glm::vec3(1.0f, 0.0f, 0.0f))},
            {0.2f, glm::quat(1.0f, 0.0f, 0.0f, 0.0f)}};
        recoil.channels.push_back(recoilArm);
        asset.clips.push_back(recoil);

        return asset;
    }

    struct LayeredTestRig
    {
        FRigAsset asset = MakeLayeredAsset();
        std::shared_ptr<Node> root;
        std::shared_ptr<Node> arm;
        std::shared_ptr<Node> leg;
        NextGameplay::FRigLayeredAnimator animator;

        LayeredTestRig()
        {
            root = Node::CreateNode("bone_root", glm::vec3(0.0f), glm::quat(1, 0, 0, 0), glm::vec3(1.0f), 10);
            arm = Node::CreateNode("bone_arm", glm::vec3(0.0f, 1.0f, 0.0f), glm::quat(1, 0, 0, 0),
                                   glm::vec3(1.0f), 11);
            leg = Node::CreateNode("bone_leg", glm::vec3(0.0f, -1.0f, 0.0f), glm::quat(1, 0, 0, 0),
                                   glm::vec3(1.0f), 12);
            arm->SetParent(root);
            leg->SetParent(root);
            animator.Bind(&asset, {root.get(), arm.get(), leg.get()}, root.get());
        }
    };
}

TEST_CASE("RigAnimator samples deterministically and wraps loop clips", "[Unit][Rig]")
{
    TestRig rig;
    rig.animator.Play("move", 0.0f);

    rig.animator.Update(0.25f);
    CHECK(rig.root->Translation().y == Catch::Approx(0.25f));

    // 0.25 + 1.0 wraps to 0.25 again.
    rig.animator.Update(1.0f);
    CHECK(rig.root->Translation().y == Catch::Approx(0.25f));

    // Uncovered bones stay at bind pose.
    CHECK(rig.arm->Translation().y == Catch::Approx(1.0f));
    CHECK(rig.arm->Scale().x == Catch::Approx(1.0f));
}

TEST_CASE("RigAnimator clamps non-loop clips at the last key", "[Unit][Rig]")
{
    TestRig rig;
    rig.animator.Play("pose", 0.0f);
    rig.animator.Update(5.0f);
    CHECK(rig.root->Translation().y == Catch::Approx(0.5f));
    rig.animator.Update(5.0f);
    CHECK(rig.root->Translation().y == Catch::Approx(0.5f));
}

TEST_CASE("RigAnimator crossfades with smoothstep weights", "[Unit][Rig]")
{
    TestRig rig;
    rig.animator.Play("move", 0.0f);
    rig.animator.Update(0.25f); // move at 0.25 -> y 0.25

    rig.animator.Play("pose", 0.2f);
    rig.animator.Update(0.1f);
    // pose target y = 0.5; move keeps advancing to 0.35 -> y 0.35.
    // halfway through the fade: smoothstep(0.5) = 0.5 -> mix(0.35, 0.5, 0.5).
    CHECK(rig.root->Translation().y == Catch::Approx(0.425f).margin(1e-5));

    rig.animator.Update(0.1f);
    // Fade complete: pure pose.
    CHECK(rig.root->Translation().y == Catch::Approx(0.5f).margin(1e-5));

    rig.animator.Update(1.0f);
    CHECK(rig.root->Translation().y == Catch::Approx(0.5f).margin(1e-5));
}

TEST_CASE("RigAnimator honors play speed and phase offset", "[Unit][Rig]")
{
    TestRig rig;
    rig.animator.SetPlaySpeed(2.0f);
    rig.animator.Play("move", 0.0f);
    rig.animator.Update(0.25f);
    CHECK(rig.root->Translation().y == Catch::Approx(0.5f));

    TestRig offsetRig;
    offsetRig.animator.SetPhaseOffset(0.3f);
    offsetRig.animator.Play("move", 0.0f);
    offsetRig.animator.Update(0.0f);
    CHECK(offsetRig.root->Translation().y == Catch::Approx(0.3f).margin(1e-5));
}

TEST_CASE("RigAnimator treats same-clip Play and unknown clips as no-ops", "[Unit][Rig]")
{
    TestRig rig;
    rig.animator.Play("move", 0.0f);
    rig.animator.Update(0.25f);

    rig.animator.Play("move", 0.2f); // same clip: must not restart or fade
    rig.animator.Update(0.0f);
    CHECK(rig.root->Translation().y == Catch::Approx(0.25f));
    CHECK(rig.animator.CurrentClip() == "move");

    rig.animator.Play("missing", 0.2f); // unknown clip: keep playing current
    rig.animator.Update(0.25f);
    CHECK(rig.animator.CurrentClip() == "move");
    CHECK(rig.root->Translation().y == Catch::Approx(0.5f));
}

TEST_CASE("RigLayeredAnimator masks upper-body overrides and preserves unauthored components", "[Unit][Rig][Layered]")
{
    LayeredTestRig rig;
    const auto baseLayer = rig.animator.CreateLayer(
        "base", NextGameplay::ERigLayerBlendMode::Override,
        NextGameplay::FRigBoneMask::FullBody(rig.asset));
    const auto aimLayer = rig.animator.CreateLayer(
        "aim", NextGameplay::ERigLayerBlendMode::Override,
        NextGameplay::FRigBoneMask::FromSubtree(rig.asset, "bone_arm"));
    REQUIRE(baseLayer != NextGameplay::invalidRigLayerHandle);
    REQUIRE(aimLayer != NextGameplay::invalidRigLayerHandle);

    REQUIRE(rig.animator.SetStaticBlend(baseLayer, {{rig.asset.FindClip("base"), 1.0f}}, 0.0f));
    REQUIRE(rig.animator.SetStaticBlend(aimLayer, {{rig.asset.FindClip("aim"), 1.0f}}, 0.0f));
    rig.animator.Update(0.0f);

    CHECK(rig.arm->Translation().x == Catch::Approx(0.2f));
    const glm::vec3 armUp = rig.arm->Rotation() * glm::vec3(0.0f, 1.0f, 0.0f);
    CHECK(armUp.x == Catch::Approx(-0.707106f).margin(1.0e-4f));
    const glm::vec3 legForward = rig.leg->Rotation() * glm::vec3(0.0f, 1.0f, 0.0f);
    CHECK(legForward.z == Catch::Approx(0.5f).margin(1.0e-4f));
}

TEST_CASE("RigLayeredAnimator blends clip samples and handles quaternion hemispheres", "[Unit][Rig][Layered]")
{
    LayeredTestRig rig;
    const auto layer = rig.animator.CreateLayer(
        "base", NextGameplay::ERigLayerBlendMode::Override,
        NextGameplay::FRigBoneMask::FullBody(rig.asset));

    REQUIRE(rig.animator.SetStaticBlend(
        layer,
        {{rig.asset.FindClip("aim_positive"), 0.5f}, {rig.asset.FindClip("aim_negative"), 0.5f}},
        0.0f));
    rig.animator.Update(0.0f);

    CHECK(rig.arm->Translation().x == Catch::Approx(0.0f).margin(1.0e-5f));
    const glm::vec3 armUp = rig.arm->Rotation() * glm::vec3(0.0f, 1.0f, 0.0f);
    CHECK(armUp.x == Catch::Approx(-0.707106f).margin(1.0e-4f));
}

TEST_CASE("RigLayeredAnimator preserves synchronized phase across loop clip sets", "[Unit][Rig][Layered]")
{
    LayeredTestRig rig;
    const auto layer = rig.animator.CreateLayer(
        "locomotion", NextGameplay::ERigLayerBlendMode::Override,
        NextGameplay::FRigBoneMask::FullBody(rig.asset));

    REQUIRE(rig.animator.SetLoopBlend(layer, {{rig.asset.FindClip("move_a"), 1.0f}},
                                      "locomotion", 1.0f, 0.0f));
    rig.animator.Update(0.25f);
    CHECK(rig.root->Translation().y == Catch::Approx(0.25f));

    REQUIRE(rig.animator.SetLoopBlend(layer, {{rig.asset.FindClip("move_b"), 1.0f}},
                                      "locomotion", 1.0f, 0.0f));
    rig.animator.Update(0.0f);
    CHECK(rig.root->Translation().y == Catch::Approx(0.5f));
}

TEST_CASE("RigLayeredAnimator composes additive recoil over an aim pose", "[Unit][Rig][Layered]")
{
    LayeredTestRig rig;
    const auto aimLayer = rig.animator.CreateLayer(
        "aim", NextGameplay::ERigLayerBlendMode::Override,
        NextGameplay::FRigBoneMask::FromSubtree(rig.asset, "bone_arm"));
    const auto recoilLayer = rig.animator.CreateLayer(
        "recoil", NextGameplay::ERigLayerBlendMode::Additive,
        NextGameplay::FRigBoneMask::FromSubtree(rig.asset, "bone_arm"));

    REQUIRE(rig.animator.SetStaticBlend(aimLayer, {{rig.asset.FindClip("aim"), 1.0f}}, 0.0f));
    REQUIRE(rig.animator.PlayOneShot(recoilLayer, rig.asset.FindClip("recoil"), 1.0f, 0.0f, 0.0f, true));
    rig.animator.Update(0.1f);

    const glm::quat aim = rig.asset.FindClip("aim")->channels[0].rotation.Keys[0].Value;
    const glm::quat kick = rig.asset.FindClip("recoil")->channels[0].rotation.Keys[1].Value;
    CHECK(glm::abs(glm::dot(rig.arm->Rotation(), glm::normalize(aim * kick))) ==
          Catch::Approx(1.0f).margin(1.0e-4f));
}

TEST_CASE("RigLayeredAnimator supports manual action time and restartable one-shots", "[Unit][Rig][Layered]")
{
    LayeredTestRig rig;
    const auto actionLayer = rig.animator.CreateLayer(
        "action", NextGameplay::ERigLayerBlendMode::Override,
        NextGameplay::FRigBoneMask::FullBody(rig.asset));
    REQUIRE(rig.animator.SetManualBlend(actionLayer, {{rig.asset.FindClip("move_a"), 1.0f}},
                                        0.6f, 0.0f));
    rig.animator.Update(0.0f);
    CHECK(rig.root->Translation().y == Catch::Approx(0.6f));

    REQUIRE(rig.animator.PlayOneShot(actionLayer, rig.asset.FindClip("move_a"), 1.0f, 0.0f, 0.0f, true));
    rig.animator.Update(0.4f);
    CHECK(rig.root->Translation().y == Catch::Approx(0.4f));

    REQUIRE(rig.animator.PlayOneShot(actionLayer, rig.asset.FindClip("move_a"), 1.0f, 0.0f, 0.0f, true));
    rig.animator.Update(0.0f);
    CHECK(rig.root->Translation().y == Catch::Approx(0.0f));
    rig.animator.Update(1.0f);
    CHECK(rig.animator.IsOneShotComplete(actionLayer));
    CHECK(rig.root->Translation().y == Catch::Approx(0.0f));
}

#include "Modules/ScadLoader/FScadRig.h"

TEST_CASE("RigAnimator drives agent_basic to a standing pose in world space", "[Unit][Rig]")
{
    Assets::FRigAsset asset;
    std::string err;
    REQUIRE(Assets::FScadRigLoader::LoadRig("assets/scad/characters/agent_basic.scad",
                                            Assets::ScadRigLoadOptions{}, asset, err));

    // Mirror FRigInstance::Instantiate without a Scene: world node + bone tree.
    auto world = Node::CreateNode("agent", glm::vec3(10.0f, 0.0f, 5.0f), glm::quat(1, 0, 0, 0), glm::vec3(1.0f), 0);
    std::vector<std::shared_ptr<Node>> nodes(asset.bones.size());
    std::vector<Node*> raw(asset.bones.size());
    for (size_t i = 0; i < asset.bones.size(); ++i)
    {
        const Assets::FRigBone& bone = asset.bones[i];
        nodes[i] = Node::CreateNode(bone.name, bone.bindT, bone.bindR, bone.bindS, static_cast<uint32_t>(i));
        nodes[i]->SetParent(bone.parent >= 0 ? nodes[bone.parent] : world);
        raw[i] = nodes[i].get();
    }
    world->RecalcTransform(true);

    FRigAnimator animator;
    animator.Bind(&asset, raw, world.get());
    animator.Play("idle", 0.0f);
    animator.Update(0.1f);

    const int headIdx = asset.FindBone("bone_head");
    REQUIRE(headIdx >= 0);
    const glm::vec3 headWorld = raw[headIdx]->WorldTranslation();
    INFO("head world = " << headWorld.x << "," << headWorld.y << "," << headWorld.z);
    CHECK(headWorld.x == Catch::Approx(10.0f).margin(0.2f));
    CHECK(headWorld.z == Catch::Approx(5.0f).margin(0.2f));
    CHECK(headWorld.y > 1.2f);   // head pivot ~1.38 above ground
    CHECK(headWorld.y < 1.6f);

    // Head part geometry tops out around 1.6m above ground.
    const Assets::FRigPart* headPart = nullptr;
    for (const Assets::FRigPart& part : asset.parts)
    {
        if (part.bone == headIdx) headPart = &part;
    }
    REQUIRE(headPart != nullptr);
    const Assets::Model& headModel = asset.partModels[headPart->modelIndex];
    const glm::vec4 localTop(0.0f, headModel.GetLocalAABBMax().y, 0.0f, 1.0f);
    const glm::vec3 worldTop = glm::vec3(raw[headIdx]->WorldTransform() * localTop);
    INFO("head top world y = " << worldTop.y);
    CHECK(worldTop.y > 1.5f);
    CHECK(worldTop.y < 1.8f);

    // Walk clip keeps the feet near the ground: leg part lowest vertex stays in [-0.1, 0.2].
    animator.Play("walk", 0.0f);
    animator.Update(0.3f);
    const int legIdx = asset.FindBone("bone_leg_l");
    REQUIRE(legIdx >= 0);
    const Assets::FRigPart* legPart = nullptr;
    for (const Assets::FRigPart& part : asset.parts)
    {
        if (part.bone == legIdx) legPart = &part;
    }
    REQUIRE(legPart != nullptr);
    const Assets::Model& legModel = asset.partModels[legPart->modelIndex];
    const glm::vec4 legBottom(0.0f, legModel.GetLocalAABBMin().y, 0.0f, 1.0f);
    const glm::vec3 legWorld = glm::vec3(raw[legIdx]->WorldTransform() * legBottom);
    INFO("leg bottom world y = " << legWorld.y);
    CHECK(legWorld.y > -0.15f);
    CHECK(legWorld.y < 0.4f);
}
