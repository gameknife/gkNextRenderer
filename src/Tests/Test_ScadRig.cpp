#include <catch2/catch_all.hpp>

#include "Engine/Assets/Data/RigAsset.hpp"
#include "Modules/ScadLoader/FScadEvaluator.h"
#include "Modules/ScadLoader/FScadLexer.h"
#include "Modules/ScadLoader/FScadParser.h"
#include "Modules/ScadLoader/FScadRig.h"

#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

using namespace Assets;
using namespace Assets::Scad;

namespace
{
    SceneEvalResult EvalRigScene(const std::string& src)
    {
        std::vector<Token> tokens;
        std::string err;
        REQUIRE(ScadLexer::Tokenize(src, tokens, err));

        Scope scope;
        REQUIRE(ScadParser::Parse(tokens, scope, err));

        std::unordered_map<std::string, StmtPtr> modules;
        std::unordered_map<std::string, StmtPtr> functions;
        Scope top;
        for (const StmtPtr& s : scope)
        {
            if (s->kind == StmtKind::ModuleDef) modules[s->name] = s;
            else if (s->kind == StmtKind::FunctionDef) functions[s->name] = s;
            else top.push_back(s);
        }

        ScadLoadOptions options;
        SceneEvalResult result;
        std::string evalErr;
        ScadEvaluator::EvaluateScene(top, modules, functions, options, result, evalErr);
        return result;
    }

    bool BuildRigFromSource(
        const std::string& src,
        FRigAsset& outAsset,
        std::vector<std::string>* warnings = nullptr,
        const ScadRigLoadOptions& options = {})
    {
        const SceneEvalResult result = EvalRigScene(src);
        std::string err;
        return FScadRigLoader::BuildRig(result, options, outAsset, err, warnings);
    }

    bool AnyWarningContains(const std::vector<std::string>& warnings, const std::string& needle)
    {
        for (const std::string& w : warnings)
        {
            if (w.find(needle) != std::string::npos)
            {
                return true;
            }
        }
        return false;
    }

    const char* kBasicRig =
        "module bone_head() { cube(0.2, center = true); }\n"
        "module bone_torso() {\n"
        "    cube([0.3, 0.2, 0.5], center = true);\n"
        "    translate([0, 0, 0.55]) bone_head();\n"
        "}\n"
        "module bone_root() { translate([0, 0, 0.84]) bone_torso(); }\n"
        "bone_root();\n";
}

TEST_CASE("ScadRig builds bone hierarchy with engine-space pivots", "[Unit][ScadRig]")
{
    FRigAsset asset;
    std::vector<std::string> warnings;
    REQUIRE(BuildRigFromSource(kBasicRig, asset, &warnings));

    REQUIRE(warnings.empty());
    REQUIRE(asset.bones.size() == 3);
    CHECK(asset.bones[0].name == "bone_root");
    CHECK(asset.bones[0].parent == -1);
    CHECK(asset.bones[1].name == "bone_torso");
    CHECK(asset.bones[1].parent == 0);
    CHECK(asset.bones[2].name == "bone_head");
    CHECK(asset.bones[2].parent == 1);
    REQUIRE(asset.bones[0].children == std::vector<int32_t>{1});
    REQUIRE(asset.bones[1].children == std::vector<int32_t>{2});

    // SCAD translate [0,0,z] (Z-up) becomes engine [0,z,0] (Y-up).
    CHECK(asset.bones[1].bindT.y == Catch::Approx(0.84f));
    CHECK(asset.bones[2].bindT.y == Catch::Approx(0.55f));
    CHECK(glm::length(asset.bones[0].bindT) == Catch::Approx(0.0f).margin(1e-6));

    CHECK(asset.FindBone("bone_head") == 2);
    CHECK(asset.FindBone("bone_missing") == -1);

    // bone_root has no geometry of its own; torso and head carry one part each.
    REQUIRE(asset.parts.size() == 2);
    REQUIRE(asset.partModels.size() == 2);
    for (const FRigPart& part : asset.parts)
    {
        CHECK(part.modelIndex >= 0);
        CHECK(part.sectionColors.size() == 1);
        CHECK_FALSE(part.sectionTintable[0]);
    }
}

TEST_CASE("ScadRig converts rotate pivots through the basis conjugation", "[Unit][ScadRig]")
{
    FRigAsset asset;
    REQUIRE(BuildRigFromSource(
        "module bone_arm() { cube(0.1, center = true); }\n"
        "module bone_root() { translate([0, 0, 1]) rotate([90, 0, 0]) bone_arm(); }\n"
        "bone_root();\n",
        asset));

    REQUIRE(asset.bones.size() == 2);
    const FRigBone& arm = asset.bones[1];
    CHECK(arm.bindT.y == Catch::Approx(1.0f).margin(1e-5));

    // SCAD Rx(90) conjugated to engine space still rotates +90 deg about engine X:
    // engine +Y maps to engine +Z.
    const glm::vec3 rotated = arm.bindR * glm::vec3(0.0f, 1.0f, 0.0f);
    CHECK(rotated.x == Catch::Approx(0.0f).margin(1e-5));
    CHECK(rotated.y == Catch::Approx(0.0f).margin(1e-5));
    CHECK(rotated.z == Catch::Approx(1.0f).margin(1e-5));
}

TEST_CASE("ScadRig collapses helper modules into the owning bone", "[Unit][ScadRig]")
{
    FRigAsset asset;
    std::vector<std::string> warnings;
    REQUIRE(BuildRigFromSource(
        "module part_box() { translate([1, 0, 0]) cube(0.1, center = true); }\n"
        "module bone_root() { part_box(); }\n"
        "bone_root();\n",
        asset, &warnings));

    CHECK(warnings.empty());
    REQUIRE(asset.bones.size() == 1);
    REQUIRE(asset.parts.size() == 1);
    REQUIRE(asset.partModels.size() == 1);

    // The helper's translate is baked into the part geometry.
    const Model& model = asset.partModels[0];
    const glm::vec3 center = (model.GetLocalAABBMin() + model.GetLocalAABBMax()) * 0.5f;
    CHECK(center.x == Catch::Approx(1.0f).margin(1e-4));
    CHECK(center.y == Catch::Approx(0.0f).margin(1e-4));
    CHECK(center.z == Catch::Approx(0.0f).margin(1e-4));
}

TEST_CASE("ScadRig warns on duplicate bone calls and keeps the first", "[Unit][ScadRig]")
{
    FRigAsset asset;
    std::vector<std::string> warnings;
    REQUIRE(BuildRigFromSource(
        "module bone_leg() { cube(0.1, center = true); }\n"
        "module bone_root() { bone_leg(); translate([1, 0, 0]) bone_leg(); }\n"
        "bone_root();\n",
        asset, &warnings));

    CHECK(asset.bones.size() == 2);
    CHECK(AnyWarningContains(warnings, "more than once"));
}

TEST_CASE("ScadRig warns on stray top-level geometry and pivot scale", "[Unit][ScadRig]")
{
    FRigAsset asset;
    std::vector<std::string> warnings;
    REQUIRE(BuildRigFromSource(
        "module bone_arm() { cube(0.1, center = true); }\n"
        "module bone_root() { scale([2, 1, 1]) bone_arm(); }\n"
        "cube(1);\n"
        "bone_root();\n",
        asset, &warnings));

    CHECK(AnyWarningContains(warnings, "ignored") );
    CHECK(AnyWarningContains(warnings, "scale"));
    REQUIRE(asset.bones.size() == 2);
}

TEST_CASE("ScadRig fails without a top-level bone call", "[Unit][ScadRig]")
{
    const SceneEvalResult result = EvalRigScene("cube(1);\n");
    FRigAsset asset;
    std::string err;
    REQUIRE_FALSE(FScadRigLoader::BuildRig(result, ScadRigLoadOptions{}, asset, err));
    CHECK(err.find("no top-level bone") != std::string::npos);
}

TEST_CASE("ScadRig parses clips with loop metadata and converts channels", "[Unit][ScadRig]")
{
    FRigAsset asset;
    std::vector<std::string> warnings;
    REQUIRE(BuildRigFromSource(
        std::string(kBasicRig) +
        "anim_walk = [\n"
        "    [\"bone_torso\", \"rot\", [[0, [90, 0, 0]], [0.4, [-90, 0, 0]], [0.8, [90, 0, 0]]]],\n"
        "    [\"bone_root\",  \"pos\", [[0, [0, 0, 0]], [0.2, [0, 0, 0.03]], [0.8, [0, 0, 0]]]],\n"
        "];\n"
        "anim_sit = [\n"
        "    [\"loop\", false],\n"
        "    [\"bone_root\", \"pos\", [[0, [0, 0, -0.42]]]],\n"
        "];\n",
        asset, &warnings));

    CHECK(warnings.empty());
    REQUIRE(asset.clips.size() == 2);

    const FRigClip* walk = asset.FindClip("walk");
    REQUIRE(walk != nullptr);
    CHECK(walk->loop);
    CHECK(walk->duration == Catch::Approx(0.8f));
    REQUIRE(walk->channels.size() == 2);

    // rot channel: euler 90 deg about SCAD X -> engine quaternion, slerp-able.
    const FRigChannel* torso = nullptr;
    const FRigChannel* root = nullptr;
    for (const FRigChannel& c : walk->channels)
    {
        if (asset.bones[c.bone].name == "bone_torso") torso = &c;
        if (asset.bones[c.bone].name == "bone_root") root = &c;
    }
    REQUIRE(torso != nullptr);
    REQUIRE(root != nullptr);
    REQUIRE(torso->rotation.Keys.size() == 3);
    const glm::vec3 rotated = torso->rotation.Keys[0].Value * glm::vec3(0.0f, 1.0f, 0.0f);
    CHECK(rotated.z == Catch::Approx(1.0f).margin(1e-5));

    // pos channel: SCAD [0,0,0.03] -> engine [0,0.03,0].
    REQUIRE(root->position.Keys.size() == 3);
    CHECK(root->position.Keys[1].Value.y == Catch::Approx(0.03f).margin(1e-6));

    // Sampling midway between key 0 and 1 lerps the position.
    FRigChannel& mutableRoot = const_cast<FRigChannel&>(*root);
    const glm::vec3 sampled = mutableRoot.position.Sample(0.1f);
    CHECK(sampled.y == Catch::Approx(0.015f).margin(1e-6));

    const FRigClip* sit = asset.FindClip("sit");
    REQUIRE(sit != nullptr);
    CHECK_FALSE(sit->loop);
    CHECK(sit->duration == Catch::Approx(0.0f));
    REQUIRE(sit->channels.size() == 1);
    REQUIRE(sit->channels[0].position.Keys.size() == 1);
    // Single-key channel samples its only key.
    FRigChannel& mutableSit = const_cast<FRigChannel&>(sit->channels[0]);
    CHECK(mutableSit.position.Sample(0.5f).y == Catch::Approx(-0.42f).margin(1e-6));
}

TEST_CASE("ScadRig drops channels for unknown bones with a warning", "[Unit][ScadRig]")
{
    FRigAsset asset;
    std::vector<std::string> warnings;
    REQUIRE(BuildRigFromSource(
        std::string(kBasicRig) +
        "anim_test = [\n"
        "    [\"bone_ghost\", \"rot\", [[0, [10, 0, 0]]]],\n"
        "    [\"bone_torso\", \"warp\", [[0, [10, 0, 0]]]],\n"
        "];\n",
        asset, &warnings));

    CHECK(AnyWarningContains(warnings, "unknown bone"));
    CHECK(AnyWarningContains(warnings, "unknown channel"));
    REQUIRE(asset.clips.size() == 1);
    CHECK(asset.clips[0].channels.empty());
}

TEST_CASE("ScadRig supports procedural keyframes via list comprehension", "[Unit][ScadRig]")
{
    FRigAsset asset;
    REQUIRE(BuildRigFromSource(
        std::string(kBasicRig) +
        "anim_idle = [\n"
        "    [\"bone_torso\", \"rot\", [for (t = [0 : 0.5 : 2]) [t, [2 * sin(180 * t), 0, 0]]]],\n"
        "];\n",
        asset));

    const FRigClip* idle = asset.FindClip("idle");
    REQUIRE(idle != nullptr);
    CHECK(idle->duration == Catch::Approx(2.0f));
    REQUIRE(idle->channels.size() == 1);
    CHECK(idle->channels[0].rotation.Keys.size() == 5);
}

TEST_CASE("ScadRig marks tint placeholder sections as tintable", "[Unit][ScadRig]")
{
    FRigAsset asset;
    REQUIRE(BuildRigFromSource(
        "ROLECOLOR = [1, 0, 1];\n"
        "module bone_root() {\n"
        "    color(ROLECOLOR) cube([0.3, 0.2, 0.5], center = true);\n"
        "    color([0.2, 0.2, 0.2]) translate([0, 0, -0.5]) cube(0.2, center = true);\n"
        "}\n"
        "bone_root();\n",
        asset));

    REQUIRE(asset.parts.size() == 1);
    const FRigPart& part = asset.parts[0];
    REQUIRE(part.sectionColors.size() == 2);
    REQUIRE(part.sectionTintable.size() == 2);

    int tintable = 0;
    for (size_t i = 0; i < part.sectionTintable.size(); ++i)
    {
        if (part.sectionTintable[i])
        {
            ++tintable;
            CHECK(part.sectionColors[i].r == Catch::Approx(1.0f));
            CHECK(part.sectionColors[i].g == Catch::Approx(0.0f));
            CHECK(part.sectionColors[i].b == Catch::Approx(1.0f));
        }
    }
    CHECK(tintable == 1);
}

TEST_CASE("ScadRig bakes mirror inside the bone body into geometry", "[Unit][ScadRig]")
{
    FRigAsset asset;
    std::vector<std::string> warnings;
    REQUIRE(BuildRigFromSource(
        "module part_arm() { translate([0.5, 0, 0]) cube(0.1, center = true); }\n"
        "module bone_arm_l() { part_arm(); }\n"
        "module bone_arm_r() { mirror([1, 0, 0]) part_arm(); }\n"
        "module bone_root() {\n"
        "    translate([-0.2, 0, 1]) bone_arm_l();\n"
        "    translate([ 0.2, 0, 1]) bone_arm_r();\n"
        "}\n"
        "bone_root();\n",
        asset, &warnings));

    CHECK(warnings.empty());
    REQUIRE(asset.bones.size() == 3);
    REQUIRE(asset.parts.size() == 2);

    // bone_arm_r's mirrored geometry sits at -0.5 on engine X (pivot local).
    const Model& right = asset.partModels[asset.parts[1].modelIndex];
    const glm::vec3 center = (right.GetLocalAABBMin() + right.GetLocalAABBMax()) * 0.5f;
    CHECK(center.x == Catch::Approx(-0.5f).margin(1e-4));
}

TEST_CASE("ScadRig loads the shipped agent_basic character", "[Unit][ScadRig]")
{
    FRigAsset asset;
    std::string err;
    std::vector<std::string> warnings;
    REQUIRE(FScadRigLoader::LoadRig("assets/scad/characters/agent_basic.scad",
                                    ScadRigLoadOptions{}, asset, err, &warnings));

    CHECK(warnings.empty());
    REQUIRE(asset.bones.size() == 7);
    CHECK(asset.bones[0].name == "bone_root");
    CHECK(asset.FindBone("bone_torso") >= 0);
    CHECK(asset.FindBone("bone_head") >= 0);
    CHECK(asset.FindBone("bone_arm_l") >= 0);
    CHECK(asset.FindBone("bone_arm_r") >= 0);
    CHECK(asset.FindBone("bone_leg_l") >= 0);
    CHECK(asset.FindBone("bone_leg_r") >= 0);

    for (const char* clip : {"idle", "walk", "sit", "work"})
    {
        const FRigClip* c = asset.FindClip(clip);
        REQUIRE(c != nullptr);
        CHECK_FALSE(c->channels.empty());
    }
    CHECK(asset.FindClip("walk")->duration == Catch::Approx(0.8f));
    CHECK(asset.FindClip("walk")->loop);
    CHECK_FALSE(asset.FindClip("sit")->loop);

    // Budget: < 500 triangles total.
    size_t triangles = 0;
    for (const Model& model : asset.partModels)
    {
        triangles += model.NumberOfIndices() / 3;
    }
    CHECK(triangles < 500);
    CHECK(triangles > 0);

    // Height ~1.6-1.8 m: head part's local top + accumulated pivots.
    // Cheap sanity: head bone pivot stacks to about 1.38 in engine Y.
    const FRigBone& torso = asset.bones[asset.FindBone("bone_torso")];
    const FRigBone& head = asset.bones[asset.FindBone("bone_head")];
    CHECK(torso.bindT.y + head.bindT.y == Catch::Approx(1.38f).margin(0.05f));

    // Tintable sections exist (torso + sleeves).
    int tintableSections = 0;
    for (const FRigPart& part : asset.parts)
    {
        for (bool t : part.sectionTintable)
        {
            if (t) ++tintableSections;
        }
    }
    CHECK(tintableSections >= 3);
}

TEST_CASE("ScadRig loads the shipped NextRA soldier", "[Unit][ScadRig][NextRA]")
{
    FRigAsset asset;
    std::string err;
    std::vector<std::string> warnings;
    REQUIRE(FScadRigLoader::LoadRig("assets/scad/characters/next_ra_soldier.scad",
                                    ScadRigLoadOptions{}, asset, err, &warnings));

    CHECK(warnings.empty());
    REQUIRE(asset.bones.size() == 7);
    CHECK(asset.bones[0].name == "bone_root");

    for (const char* clip : {"idle", "walk", "fire"})
    {
        const FRigClip* c = asset.FindClip(clip);
        REQUIRE(c != nullptr);
        CHECK_FALSE(c->channels.empty());
    }
    CHECK(asset.FindClip("walk")->duration == Catch::Approx(0.8f));
    CHECK(asset.FindClip("walk")->loop);
    CHECK_FALSE(asset.FindClip("fire")->loop);

    size_t triangles = 0;
    int tintableSections = 0;
    for (const Model& model : asset.partModels)
    {
        triangles += model.NumberOfIndices() / 3;
    }
    for (const FRigPart& part : asset.parts)
    {
        for (bool tintable : part.sectionTintable)
        {
            if (tintable) ++tintableSections;
        }
    }
    CHECK(triangles > 0);
    CHECK(triangles < 600);
    CHECK(tintableSections >= 4);

    const FRigBone& torso = asset.bones[asset.FindBone("bone_torso")];
    const FRigBone& head = asset.bones[asset.FindBone("bone_head")];
    CHECK(torso.bindT.y + head.bindT.y == Catch::Approx(1.44f).margin(0.02f));
}

TEST_CASE("ScadRig loads the articulated NextDayz survivor contract", "[Unit][ScadRig][NextDayz]")
{
    FRigAsset asset;
    std::string err;
    std::vector<std::string> warnings;
    REQUIRE(FScadRigLoader::LoadRig("assets/scad/characters/nextdayz_survivor.scad",
                                    ScadRigLoadOptions{}, asset, err, &warnings));

    INFO("warnings: " << warnings.size());
    CHECK(warnings.empty());
    REQUIRE(asset.bones.size() == 17);
    for (const char* bone : {
             "bone_root", "bone_pelvis", "bone_torso", "bone_head",
             "bone_upperarm_l", "bone_forearm_l", "bone_hand_l",
             "bone_upperarm_r", "bone_forearm_r", "bone_hand_r", "bone_weapon_socket",
             "bone_thigh_l", "bone_calf_l", "bone_foot_l",
             "bone_thigh_r", "bone_calf_r", "bone_foot_r"})
    {
        CHECK(asset.FindBone(bone) >= 0);
    }

    const int32_t socket = asset.FindBone("bone_weapon_socket");
    REQUIRE(socket >= 0);
    CHECK(asset.bones[socket].parent == asset.FindBone("bone_hand_r"));
    bool socketHasPart = false;
    for (const FRigPart& part : asset.parts)
    {
        socketHasPart = socketHasPart || part.bone == socket;
    }
    CHECK_FALSE(socketHasPart);

    const std::vector<std::string> loopClips = {
        "stand_idle",
        "stand_walk_f", "stand_walk_b", "stand_walk_l", "stand_walk_r",
        "stand_run_f", "stand_run_b", "stand_run_l", "stand_run_r",
        "stand_sprint_f", "stand_sprint_b", "stand_sprint_l", "stand_sprint_r",
        "crouch_idle", "crouch_walk_f", "crouch_walk_b", "crouch_walk_l", "crouch_walk_r",
        "jump_air_loop",
        "weapon_ready", "aim_rifle_down", "aim_rifle_center", "aim_rifle_up"};
    for (const std::string& clipName : loopClips)
    {
        const FRigClip* clip = asset.FindClip(clipName);
        REQUIRE(clip != nullptr);
        CHECK(clip->loop);
        CHECK_FALSE(clip->channels.empty());
    }
    for (const char* clipName : {
             "jump_up", "jump_down",
             "reload_rifle", "switch_weapon", "recoil_rifle", "loot_ground"})
    {
        const FRigClip* clip = asset.FindClip(clipName);
        REQUIRE(clip != nullptr);
        CHECK_FALSE(clip->loop);
        CHECK(clip->duration > 0.0f);
    }

    auto checkDirectionalDurations = [&asset](std::string_view prefix)
    {
        const FRigClip* forward = asset.FindClip(std::string(prefix) + "_f");
        REQUIRE(forward != nullptr);
        for (const char* direction : {"_b", "_l", "_r"})
        {
            const FRigClip* clip = asset.FindClip(std::string(prefix) + direction);
            REQUIRE(clip != nullptr);
            CHECK(clip->duration == Catch::Approx(forward->duration).epsilon(0.02));
        }
    };
    checkDirectionalDurations("stand_walk");
    checkDirectionalDurations("stand_run");
    checkDirectionalDurations("stand_sprint");
    checkDirectionalDurations("crouch_walk");

    const int32_t root = asset.FindBone("bone_root");
    for (const FRigClip& clip : asset.clips)
    {
        for (const FRigChannel& channel : clip.channels)
        {
            if (channel.bone != root)
            {
                continue;
            }
            for (const auto& key : channel.position.Keys)
            {
                CHECK(std::abs(key.Value.x) < 0.02f);
                CHECK(std::abs(key.Value.z) < 0.02f);
            }
        }
    }

    const FRigClip* recoil = asset.FindClip("recoil_rifle");
    REQUIRE(recoil != nullptr);
    for (const FRigChannel& channel : recoil->channels)
    {
        if (channel.rotation.Keys.empty())
        {
            continue;
        }
        CHECK(glm::abs(glm::dot(channel.rotation.Keys.front().Value, glm::quat(1, 0, 0, 0))) ==
              Catch::Approx(1.0f).margin(1.0e-5f));
        CHECK(glm::abs(glm::dot(channel.rotation.Keys.back().Value, glm::quat(1, 0, 0, 0))) ==
              Catch::Approx(1.0f).margin(1.0e-5f));
    }

    size_t triangles = 0;
    for (const Model& model : asset.partModels)
    {
        triangles += model.NumberOfIndices() / 3;
    }
    INFO("NextDayz survivor triangles: " << triangles);
    CHECK(triangles > 0);
    CHECK(triangles < 1500);
}

// kit_char 组装角色：use <../lib/kit_char.scad> 闭包 + 库函数 clip 的完整链路。
static void CheckKitCharacter(const char* path, int expectedTintable)
{
    FRigAsset asset;
    std::string err;
    std::vector<std::string> warnings;
    REQUIRE(FScadRigLoader::LoadRig(path, ScadRigLoadOptions{}, asset, err, &warnings));

    CHECK(warnings.empty());
    REQUIRE(asset.bones.size() == 7);
    CHECK(asset.bones[0].name == "bone_root");
    for (const char* bone : {"bone_torso", "bone_head", "bone_arm_l", "bone_arm_r",
                             "bone_leg_l", "bone_leg_r"})
    {
        CHECK(asset.FindBone(bone) >= 0);
    }

    // clip 来自 kit 函数（anim_x = ch_clip_x()），与 agent_basic 语义一致。
    for (const char* clip : {"idle", "walk", "sit", "work", "wave"})
    {
        const FRigClip* c = asset.FindClip(clip);
        REQUIRE(c != nullptr);
        CHECK_FALSE(c->channels.empty());
    }
    CHECK(asset.FindClip("walk")->duration == Catch::Approx(0.8f));
    CHECK(asset.FindClip("walk")->loop);
    CHECK_FALSE(asset.FindClip("sit")->loop);

    // 骨架标准 pivot：torso(0.84) + head(0.54) = 1.38（引擎 Y）。
    const FRigBone& torso = asset.bones[asset.FindBone("bone_torso")];
    const FRigBone& head = asset.bones[asset.FindBone("bone_head")];
    CHECK(torso.bindT.y + head.bindT.y == Catch::Approx(1.38f).margin(0.02f));

    size_t triangles = 0;
    for (const Model& model : asset.partModels)
    {
        triangles += model.NumberOfIndices() / 3;
    }
    CHECK(triangles > 0);
    CHECK(triangles < 900);

    int tintableSections = 0;
    for (const FRigPart& part : asset.parts)
    {
        for (bool tintable : part.sectionTintable)
        {
            if (tintable) ++tintableSections;
        }
    }
    CHECK(tintableSections >= expectedTintable);
}

TEST_CASE("ScadRig loads the kit_char worker character", "[Unit][ScadRig][KitChar]")
{
    CheckKitCharacter("assets/scad/characters/worker.scad", 3); // 背心 + 双手套袖
}

TEST_CASE("ScadRig loads the kit_char citizen character", "[Unit][ScadRig][KitChar]")
{
    CheckKitCharacter("assets/scad/characters/citizen.scad", 3); // 连衣裙 + 双袖
}
