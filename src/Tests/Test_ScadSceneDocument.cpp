#include <catch2/catch_all.hpp>

#include "Application/Editor/ScadLibrary/ScadSceneDocument.hpp"
#include "Modules/ScadLoader/FScadEvaluator.h"
#include "Modules/ScadLoader/FScadSourceIndex.h"
#include "Modules/ScadLoader/FScadTypes.h"

#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

using namespace Assets;
using namespace Assets::Scad;
using namespace ScadLibrary;

namespace
{
    struct FIndexedSource
    {
        FScadSourceIndex index;
        std::map<std::string, Value> variables;
    };

    FIndexedSource IndexAndEvaluate(const std::string& source)
    {
        FIndexedSource result;
        std::string error;
        REQUIRE(BuildScadSourceIndex(source, result.index, error));

        std::unordered_map<std::string, StmtPtr> modules;
        std::unordered_map<std::string, StmtPtr> functions;
        Scope topLevel;
        for (const StmtPtr& statement : result.index.topLevel)
        {
            if (statement->kind == StmtKind::ModuleDef)
            {
                modules[statement->name] = statement;
            }
            else if (statement->kind == StmtKind::FunctionDef)
            {
                functions[statement->name] = statement;
            }
            else
            {
                topLevel.push_back(statement);
            }
        }

        ScadLoadOptions options;
        SceneEvalResult evaluated;
        REQUIRE(ScadEvaluator::EvaluateScene(topLevel, modules, functions, options, evaluated, error));
        result.variables = std::move(evaluated.topLevelVariables);
        return result;
    }

    // Kit modules the fixture scenes place. Everything else (builtins, local
    // modules) has to stay a Source statement.
    bool IsFixtureKitModule(const std::string& name)
    {
        return name == "kit_crate" || name == "kit_lamp" || name == "kit_wall";
    }

    FScadSceneDocument ParseDocument(const std::string& source, std::vector<std::string>& warnings)
    {
        const FIndexedSource indexed = IndexAndEvaluate(source);
        FScadSceneDocument document;
        REQUIRE(document.Parse(source, indexed.index, indexed.variables, IsFixtureKitModule, warnings));
        return document;
    }

    size_t CountSegments(const FScadSceneDocument& document, EScadSegmentKind kind)
    {
        size_t count = 0;
        for (const FScadSceneSegment& segment : document.Segments())
        {
            if (segment.kind == kind)
            {
                ++count;
            }
        }
        return count;
    }

    int FindSegment(const FScadSceneDocument& document, EScadSegmentKind kind, const std::string& name,
                    StmtKind statementKind = StmtKind::Instance)
    {
        for (size_t index = 0; index < document.Segments().size(); ++index)
        {
            const FScadSceneSegment& segment = document.Segments()[index];
            if (segment.kind == kind && segment.name == name && segment.statementKind == statementKind)
            {
                return static_cast<int>(index);
            }
        }
        return -1;
    }

    std::filesystem::path FindRepoAsset(const std::string& relativePath)
    {
        std::filesystem::path cursor = std::filesystem::current_path();
        while (!cursor.empty())
        {
            const std::filesystem::path candidate = cursor / relativePath;
            if (std::filesystem::is_regular_file(candidate))
            {
                return candidate;
            }
            const std::filesystem::path parent = cursor.parent_path();
            if (parent == cursor)
            {
                break;
            }
            cursor = parent;
        }
        return {};
    }

    std::string ReadRepoAsset(const std::filesystem::path& path)
    {
        std::ifstream input(path, std::ios::binary);
        REQUIRE(input.is_open());
        return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    }

    // A program that mixes all three former "kinds" in one root file: free
    // structure (a module definition and a loop), hand-placed kit instances,
    // and a terrain with its procedural rules.
    const char* kMixedScene = R"SCAD(use <../lib/kit_demo.scad>

TERR = ["gkterr1", [200, 200], [40, 40], 7, [0, 1.5, 0.35], undef, "temperate",
    [
        ["mountain", [40, 40], 30, 18, 0.6]
    ]];

module row_of_crates(count = 3)
{
    for (i = [0 : count - 1])
    {
        translate([i * 4, 0, 0]) kit_crate();
    }
}

gk_terrain(TERR);

ter_place(TERR, 10, -12) kit_lamp();

row_of_crates(count = 4);

translate([12.0000, -3.0000, 0.0000]) rotate([0.0000, 0.0000, 90.0000]) scale([1.00000, 1.00000, 1.00000]) kit_wall();

color([0.20000, 0.40000, 0.90000, 1.00000]) translate([0.0000, 8.0000, 0.0000]) rotate([0.0000, 0.0000, 0.0000]) scale([2.00000, 2.00000, 2.00000]) kit_lamp(seed = 3);

cube([2, 2, 2], center = true);
)SCAD";
} // namespace

TEST_CASE("Scad scene document classifies every top-level node on its own", "[Unit][Scad][ScadLibrary][SceneDocument]")
{
    std::vector<std::string> warnings;
    const FScadSceneDocument document = ParseDocument(kMixedScene, warnings);

    // The three former file kinds now coexist in one root file.
    CHECK(document.HasTerrain());
    CHECK(document.Instances().size() == 2);
    CHECK(CountSegments(document, EScadSegmentKind::Instance) == 2);
    CHECK(CountSegments(document, EScadSegmentKind::TerrainRule) == 1);
    CHECK(CountSegments(document, EScadSegmentKind::Terrain) == 2); // TERR assignment + gk_terrain call

    // A local module definition, its loop call and a free primitive stay source.
    CHECK(FindSegment(document, EScadSegmentKind::Source, "row_of_crates", StmtKind::ModuleDef) >= 0);
    CHECK(FindSegment(document, EScadSegmentKind::Source, "row_of_crates", StmtKind::Instance) >= 0);
    CHECK(FindSegment(document, EScadSegmentKind::Source, "cube") >= 0);

    // A module definition cannot carry OpenSCAD's `*` modifier, so the editor
    // refuses to switch it off.
    CHECK_FALSE(
        document.IsSwitchable(static_cast<size_t>(FindSegment(document, EScadSegmentKind::Source, "row_of_crates",
                                                              StmtKind::ModuleDef))));

    const FBenchItem& wall = document.Instances()[0];
    CHECK(wall.moduleName == "kit_wall");
    CHECK(wall.x == Catch::Approx(12.0f));
    CHECK(wall.rotZ == Catch::Approx(90.0f));
    CHECK(!wall.hasColor);

    const FBenchItem& lamp = document.Instances()[1];
    CHECK(lamp.moduleName == "kit_lamp");
    CHECK(lamp.hasColor);
    CHECK(lamp.color[2] == Catch::Approx(0.9f));
    CHECK(lamp.scale == Catch::Approx(2.0f));
    CHECK(std::string(lamp.args) == "seed = 3");
}

TEST_CASE("Scad scene document leaves untouched files byte-identical", "[Unit][Scad][ScadLibrary][SceneDocument]")
{
    std::vector<std::string> warnings;
    const FScadSceneDocument document = ParseDocument(kMixedScene, warnings);
    CHECK(document.BuildSource({}) == std::string(kMixedScene));
}

TEST_CASE("Scad scene document rewrites only the instance that moved", "[Unit][Scad][ScadLibrary][SceneDocument]")
{
    std::vector<std::string> warnings;
    FScadSceneDocument document = ParseDocument(kMixedScene, warnings);

    document.Instances()[0].x = 20.0f;
    const std::string written = document.BuildSource({});

    CHECK(written.find("translate([20.0000, -3.0000, 0.0000])") != std::string::npos);
    // Everything else - the loop, the module definition, the terrain rule, the
    // second instance and the free cube - survives unchanged.
    CHECK(written.find("module row_of_crates(count = 3)") != std::string::npos);
    CHECK(written.find("row_of_crates(count = 4);") != std::string::npos);
    CHECK(written.find("ter_place(TERR, 10, -12) kit_lamp();") != std::string::npos);
    CHECK(written.find("cube([2, 2, 2], center = true);") != std::string::npos);
    CHECK(written.find("kit_lamp(seed = 3);") != std::string::npos);

    std::vector<std::string> reparseWarnings;
    const FScadSceneDocument reparsed = ParseDocument(written, reparseWarnings);
    CHECK(reparsed.Instances().size() == 2);
    CHECK(reparsed.Instances()[0].x == Catch::Approx(20.0f));
    CHECK(reparsed.HasTerrain());
}

TEST_CASE("Scad scene document switches one structure off in place", "[Unit][Scad][ScadLibrary][SceneDocument]")
{
    std::vector<std::string> warnings;
    FScadSceneDocument document = ParseDocument(kMixedScene, warnings);

    const int loopSegment = FindSegment(document, EScadSegmentKind::Source, "row_of_crates");
    REQUIRE(loopSegment >= 0);
    REQUIRE(document.SetSegmentDisabled(static_cast<size_t>(loopSegment), true));

    const std::string written = document.BuildSource({});
    CHECK(written.find("*row_of_crates(count = 4);") != std::string::npos);
    // The module definition itself is a different statement and stays enabled.
    CHECK(written.find("module row_of_crates(count = 3)") != std::string::npos);

    std::vector<std::string> reparseWarnings;
    FScadSceneDocument reparsed = ParseDocument(written, reparseWarnings);
    const int reparsedSegment = FindSegment(reparsed, EScadSegmentKind::Source, "row_of_crates");
    REQUIRE(reparsedSegment >= 0);
    // Two statements carry that name; the call is the disabled one.
    bool sawDisabled = false;
    for (const FScadSceneSegment& segment : reparsed.Segments())
    {
        sawDisabled = sawDisabled || (segment.name == "row_of_crates" && segment.disabled);
    }
    CHECK(sawDisabled);

    // Switching it back on restores the original bytes.
    for (size_t index = 0; index < reparsed.Segments().size(); ++index)
    {
        if (reparsed.Segments()[index].disabled)
        {
            REQUIRE(reparsed.SetSegmentDisabled(index, false));
        }
    }
    CHECK(reparsed.BuildSource({}) == std::string(kMixedScene));
}

TEST_CASE("Scad scene document explodes one structure into instances", "[Unit][Scad][ScadLibrary][SceneDocument]")
{
    std::vector<std::string> warnings;
    FScadSceneDocument document = ParseDocument(kMixedScene, warnings);

    const int loopSegment = FindSegment(document, EScadSegmentKind::Source, "row_of_crates");
    REQUIRE(loopSegment >= 0);

    std::vector<FBenchItem> produced;
    for (int index = 0; index < 4; ++index)
    {
        FBenchItem item;
        item.moduleName = "kit_crate";
        item.x = static_cast<float>(index) * 4.0f;
        produced.push_back(item);
    }

    std::string error;
    REQUIRE(document.ExplodeSegment(static_cast<size_t>(loopSegment), produced, error));
    CHECK(document.Instances().size() == 6);

    const std::string written = document.BuildSource({{"../lib/kit_demo.scad"}});
    // The generating structure is switched off, not deleted.
    CHECK(written.find("*row_of_crates(count = 4);") != std::string::npos);
    CHECK(written.find("scale([1.00000, 1.00000, 1.00000]) kit_crate();") != std::string::npos);
    // The kit it already used is not declared a second time.
    CHECK(written.find("use <../lib/kit_demo.scad>") != std::string::npos);
    CHECK(written.find("use <../lib/kit_demo.scad>\nuse <../lib/kit_demo.scad>") == std::string::npos);

    std::vector<std::string> reparseWarnings;
    const FScadSceneDocument reparsed = ParseDocument(written, reparseWarnings);
    CHECK(reparsed.Instances().size() == 6);
    CHECK(reparsed.HasTerrain());

    // Collapsing before a save is the exact inverse.
    FScadSceneDocument collapsible = ParseDocument(kMixedScene, warnings);
    const int collapseSegment = FindSegment(collapsible, EScadSegmentKind::Source, "row_of_crates");
    REQUIRE(collapsible.ExplodeSegment(static_cast<size_t>(collapseSegment), produced, error));
    REQUIRE(collapsible.CollapseSegment(static_cast<size_t>(collapseSegment)));
    CHECK(collapsible.Instances().size() == 2);
    CHECK(collapsible.BuildSource({}) == std::string(kMixedScene));
}

TEST_CASE("Scad scene document adds and removes instances in a program file",
          "[Unit][Scad][ScadLibrary][SceneDocument]")
{
    std::vector<std::string> warnings;
    FScadSceneDocument document = ParseDocument(kMixedScene, warnings);

    FBenchItem added;
    added.moduleName = "kit_crate";
    added.x = 5.0f;
    added.y = 6.0f;
    document.AddInstance(added);

    document.RemoveInstance(0); // the kit_wall parsed from the file

    const std::string written = document.BuildSource({{"../lib/kit_extra.scad"}});
    CHECK(written.find("kit_wall();") == std::string::npos);
    CHECK(written.find("translate([5.0000, 6.0000, 0.0000])") != std::string::npos);
    CHECK(written.find("use <../lib/kit_extra.scad>") != std::string::npos);

    std::vector<std::string> reparseWarnings;
    const FScadSceneDocument reparsed = ParseDocument(written, reparseWarnings);
    CHECK(reparsed.Instances().size() == 2);
    CHECK(reparsed.HasTerrain());
}

TEST_CASE("Scad source index reports exact top-level statement spans", "[Unit][Scad][SourceIndex]")
{
    const std::string source = "a = 1; b = 2;\nmodule m() { cube(1); }\ntranslate([1,2,3]) m();\n";
    FScadSourceIndex index;
    std::string error;
    REQUIRE(BuildScadSourceIndex(source, index, error));
    REQUIRE(index.statements.size() == 4);
    REQUIRE(index.statements.size() == index.topLevel.size());

    // Two statements on one line still get their own byte range.
    CHECK(source.substr(index.statements[0].begin, index.statements[0].end - index.statements[0].begin) == "a = 1;");
    CHECK(source.substr(index.statements[1].begin, index.statements[1].end - index.statements[1].begin) == "b = 2;");
    CHECK(source.substr(index.statements[2].begin, index.statements[2].end - index.statements[2].begin) ==
          "module m() { cube(1); }");
    CHECK(source.substr(index.statements[3].begin, index.statements[3].end - index.statements[3].begin) ==
          "translate([1,2,3]) m();");
    CHECK(index.statements[3].kind == StmtKind::Instance);
    CHECK_FALSE(index.statements[3].Disabled());
}

TEST_CASE("Scad source edits apply back to front without corrupting neighbours", "[Unit][Scad][SourceIndex]")
{
    const std::string source = "0123456789";
    std::vector<FScadSourceEdit> edits;
    edits.push_back({0, 2, "AB"});
    edits.push_back({5, 5, "-"});   // insertion
    edits.push_back({5, 5, "+"});   // second insertion at the same offset
    edits.push_back({8, 10, ""});   // deletion
    CHECK(ApplyScadSourceEdits(source, edits) == "AB234-+567");

    // An overlapping (stale) span is dropped rather than corrupting the file.
    std::vector<FScadSourceEdit> overlapping;
    overlapping.push_back({2, 6, "X"});
    overlapping.push_back({4, 8, "Y"});
    const std::string result = ApplyScadSourceEdits(source, overlapping);
    CHECK(result == "0123Y89");
}

// Regression guard on real repository content: the editor must be able to open
// scenes from every assets/scad directory, classify their nodes, and write them
// back untouched. Before the unified document these files were each locked to a
// single editor by the directory they sat in.
TEST_CASE("Scad scene document opens shipped scenes without rewriting them",
          "[Unit][Scad][ScadLibrary][SceneDocument]")
{
    struct FCase
    {
        const char* path;
        bool expectTerrain;
    };
    const FCase cases[] = {
        {"assets/scad/source/oldcity/old_city.scad", false},
        {"assets/scad/proc/terrain_layout_demo.scad", true},
    };

    for (const FCase& testCase : cases)
    {
        const std::filesystem::path assetPath = FindRepoAsset(testCase.path);
        if (assetPath.empty())
        {
            WARN("skipping missing asset: " << testCase.path);
            continue;
        }
        INFO("scene: " << testCase.path);
        const std::string source = ReadRepoAsset(assetPath);

        const FIndexedSource indexed = IndexAndEvaluate(source);
        FScadSceneDocument document;
        std::vector<std::string> warnings;
        // Any module the file places counts as a kit module here; the editor
        // resolves the real kit tables instead.
        REQUIRE(document.Parse(source, indexed.index, indexed.variables,
                               [](const std::string& name)
                               { return name.rfind("oc_", 0) == 0 || name.rfind("oh_", 0) == 0; },
                               warnings));

        CHECK(document.Segments().size() == indexed.index.statements.size());
        CHECK(document.HasTerrain() == testCase.expectTerrain);
        // Opening and saving without touching anything is a no-op.
        CHECK(document.BuildSource({}) == source);
    }
}
