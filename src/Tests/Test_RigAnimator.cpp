#include <catch2/catch_all.hpp>

#include "Engine/Assets/Core/Node.h"
#include "Engine/Assets/Data/RigAsset.hpp"
#include "Gameplay/Rig/RigInstance.h"

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
