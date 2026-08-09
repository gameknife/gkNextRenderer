#include <catch2/catch_test_macros.hpp>

#include "AI/Adapters/KitModuleAIAdapter.hpp"
#include "AI/Adapters/RigClipAIAdapter.hpp"
#include "AI/Adapters/SceneObjectsAIAdapter.hpp"
#include "AI/Adapters/SceneSourceAIAdapter.hpp"
#include "AI/Adapters/TerrainProcessAIAdapter.hpp"

using namespace ScadLibrary::AI;

TEST_CASE("Scad AI adapters select OpenAI-compatible schema strictness",
          "[AI][ScadLibrary][Schema]")
{
    const FScadAIEditTarget kitTarget{
        EScadAIEditKind::KitModule, "kit.scad", "Kit", "lamp", {}};
    const std::string kit = "module lamp(size = 1) { cube(size); }\n";
    REQUIRE(FKitModuleAIAdapter::BuildRequest(kitTarget, {}, kit, "taller").strictSchema);

    const FScadAIEditTarget sceneTarget{
        EScadAIEditKind::SceneObjects, "scene.scad", "Scene", "", {}};
    const auto sceneRequest =
        FSceneObjectsAIAdapter::BuildRequest(sceneTarget, {},
                                             {{"selectedObject", {{"id", "o2"}}},
                                              {"selectionScope", "selected_instance"}},
                                             "edit");
    REQUIRE_FALSE(sceneRequest.strictSchema);
    REQUIRE(sceneRequest.snapshot["selectedObject"]["id"] == "o2");
    REQUIRE(sceneRequest.systemPrompt.find("primary edit subject") != std::string::npos);

    const FScadAIEditTarget terrainTarget{
        EScadAIEditKind::TerrainProcess, "terrain.scad", "Terrain", "", {}};
    REQUIRE_FALSE(FTerrainProcessAIAdapter::BuildRequest(
                      terrainTarget, {}, nlohmann::json::object(), "edit")
                      .strictSchema);

    const FScadAIEditTarget rigTarget{
        EScadAIEditKind::RigClip, "rig.scad", "Rig", "", {}};
    REQUIRE_FALSE(FRigClipAIAdapter::BuildRequest(rigTarget, {}, nlohmann::json::object(), "edit")
                      .strictSchema);
}

TEST_CASE("Kit module adapter replaces only the selected definition", "[AI][ScadLibrary][Kit]")
{
    const std::string kit =
        "function helper(x) = x * 2;\n"
        "module lamp(size = 1) { cube(helper(size)); }\n"
        "module chair() { cube(1); }\n";
    const std::string response = R"json({
        "version":1,
        "summary":"make lamp taller",
        "moduleName":"lamp",
        "moduleSource":"module lamp(size = 1) { cube([size, size, helper(size)]); }"
    })json";
    const FScadAIValidationResult result = FKitModuleAIAdapter::Validate(kit, "lamp", response);
    REQUIRE(result.success);
    const std::string candidate = result.candidate.at("kitSource").get<std::string>();
    REQUIRE(candidate.find("function helper(x) = x * 2;") != std::string::npos);
    REQUIRE(candidate.find("module chair() { cube(1); }") != std::string::npos);
    REQUIRE(candidate.find("cube([size, size, helper(size)])") != std::string::npos);
}

TEST_CASE("Kit module adapter locks the public signature", "[AI][ScadLibrary][Kit]")
{
    const std::string kit = "module lamp(size = 1) { cube(size); }\n";
    const std::string response = R"({
        "version":1,"summary":"bad","moduleName":"lamp",
        "moduleSource":"module lamp(size = 2, extra = 1) { cube(size); }"
    })";
    const FScadAIValidationResult result = FKitModuleAIAdapter::Validate(kit, "lamp", response);
    REQUIRE_FALSE(result.success);
    REQUIRE(result.issues.front().code == "signature");
}

TEST_CASE("Scene source adapter validates complete SCAD", "[AI][ScadLibrary][Scene]")
{
    const auto valid = FSceneSourceAIAdapter::Validate(
        "cube(1);", R"({"version":1,"summary":"two cubes","source":"cube(1); translate([2,0,0]) cube(1);"})");
    REQUIRE(valid.success);
    const auto invalid = FSceneSourceAIAdapter::Validate(
        "cube(1);", R"({"version":1,"summary":"broken","source":"cube(["})");
    REQUIRE_FALSE(invalid.success);
}

TEST_CASE("Scene object operations keep snapshot ids stable after removal", "[AI][ScadLibrary][Scene]")
{
    const nlohmann::json snapshot = {
        {"catalog", {{{"kitIndex", 0}, {"module", "box"}}}},
        {"objects",
         {
             {{"id", "o0"}, {"kitIndex", 0}, {"module", "box"}, {"position", {0, 0, 0}},
              {"rotation", {0, 0, 0}}, {"scale", {1, 1, 1}}, {"arguments", ""}},
             {{"id", "o1"}, {"kitIndex", 0}, {"module", "box"}, {"position", {1, 0, 0}},
              {"rotation", {0, 0, 0}}, {"scale", {1, 1, 1}}, {"arguments", ""}},
         }},
    };
    const std::string response = R"json({
      "version":1,"summary":"remove first and move second",
      "operations":[
        {"type":"remove","id":"o0"},
        {"type":"update","id":"o1","changes":{"position":[7,8,9]}}
      ]
    })json";
    const FScadAIValidationResult result = FSceneObjectsAIAdapter::Validate(snapshot, response);
    REQUIRE(result.success);
    REQUIRE(result.candidate["objects"].size() == 1);
    REQUIRE(result.candidate["objects"][0]["id"] == "o1");
    REQUIRE(result.candidate["objects"][0]["position"] == nlohmann::json::array({7, 8, 9}));
}

TEST_CASE("Scene object adapter rejects argument text that escapes the module call",
          "[AI][ScadLibrary][Scene]")
{
    const nlohmann::json snapshot = {
        {"catalog", {{{"kitIndex", 0}, {"module", "box"}}}},
        {"objects", {{{"id", "o0"}, {"kitIndex", 0}, {"module", "box"}, {"position", {0, 0, 0}},
                      {"rotation", {0, 0, 0}}, {"scale", {1, 1, 1}}, {"arguments", ""}}}},
    };
    const std::string response = R"json({
      "version":1,"summary":"inject",
      "operations":[{"type":"update","id":"o0","changes":{"arguments":"1); sphere(9"}}]
    })json";
    REQUIRE_FALSE(FSceneObjectsAIAdapter::Validate(snapshot, response).success);
}

TEST_CASE("Terrain operations address feature ids instead of indices", "[AI][ScadLibrary][Terrain]")
{
    const nlohmann::json feature = {
        {"id", "f0"}, {"type", "river"}, {"at", {0, 0}}, {"size", {1, 1}}, {"rotation", 0},
        {"radius", 1}, {"height", 0}, {"depth", 2}, {"width", 3}, {"rugged", 0},
        {"points", {{0, 0}, {10, 0}}},
    };
    const nlohmann::json snapshot = {
        {"terrain", {{"size", {100, 100}}, {"cells", {50, 50}}}},
        {"features", {feature}},
        {"rules", nlohmann::json::array()},
    };
    const std::string response = R"({
      "version":1,"summary":"widen river",
      "operations":[{"type":"update_feature","id":"f0","changes":{"width":6}}]
    })";
    const FScadAIValidationResult result = FTerrainProcessAIAdapter::Validate(snapshot, response);
    REQUIRE(result.success);
    REQUIRE(result.candidate["features"][0]["width"] == 6);
}

TEST_CASE("Rig adapter can add a typed non-loop clip", "[AI][ScadLibrary][Rig]")
{
    const nlohmann::json snapshot = {
        {"bones", {"bone_root", "bone_arm_l"}},
        {"clips", nlohmann::json::array()},
    };
    const std::string response = R"json({
      "version":1,"summary":"add wave",
      "operations":[{"type":"create_clip","clip":{
        "name":"wave","loop":false,
        "channels":[{"bone":"bone_arm_l","channel":"rot","keys":[
          {"time":0,"value":[0,0,-20]},
          {"time":0.5,"value":[0,0,40]},
          {"time":1,"value":[0,0,-20]}
        ]}]
      }}]
    })json";
    const FScadAIValidationResult result = FRigClipAIAdapter::Validate(snapshot, response);
    REQUIRE(result.success);
    REQUIRE(result.candidate["clips"].size() == 1);
    REQUIRE(result.candidate["clips"][0]["name"] == "wave");
    REQUIRE(result.candidate["clips"][0]["loop"] == false);
    REQUIRE(result.candidate["clips"][0]["duration"] == 1.0);
}

TEST_CASE("Rig adapter rejects unknown bones and non-positive scale", "[AI][ScadLibrary][Rig]")
{
    const nlohmann::json snapshot = {{"bones", {"bone_root"}}, {"clips", nlohmann::json::array()}};
    const std::string response = R"json({
      "version":1,"summary":"bad",
      "operations":[{"type":"create_clip","clip":{
        "name":"bad","loop":true,
        "channels":[{"bone":"missing","channel":"scale","keys":[{"time":0,"value":[1,0,1]}]}]
      }}]
    })json";
    REQUIRE_FALSE(FRigClipAIAdapter::Validate(snapshot, response).success);
}
