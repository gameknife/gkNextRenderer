#include <catch2/catch_all.hpp>

#include "Engine/Assets/Core/Model.hpp"
#include "Engine/Assets/Core/Node.h"
#include "Engine/Assets/Data/Material.hpp"
#include "Engine/Assets/Loaders/FScadCsg.h"
#include "Engine/Assets/Loaders/FScadEvaluator.h"
#include "Engine/Assets/Loaders/FScadLexer.h"
#include "Engine/Assets/Loaders/FScadLoader.h"
#include "Engine/Assets/Loaders/FScadParser.h"
#include "Engine/Assets/Loaders/FScadText.h"
#include "Engine/Runtime/Components/RenderComponent.h"
#include "Engine/Utilities/FileHelper.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>

using namespace Assets::scad;

namespace
{
    // Drives lex -> parse -> split defs -> evaluate, like the loader does.
    EvalResult EvalProgram(const std::string& src)
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

        Assets::ScadLoadOptions options;
        EvalResult result;
        std::string evalErr;
        ScadEvaluator::Evaluate(top, modules, functions, options, result, evalErr);
        return result;
    }

    size_t TotalTriangles(const EvalResult& result)
    {
        size_t tris = 0;
        for (const auto& entry : result.buckets) tris += entry.second.tris.size() / 3;
        return tris;
    }

    class ScopedDir
    {
    public:
        ScopedDir()
        {
            const auto suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
            dir_ = std::filesystem::temp_directory_path() / ("scad_test_" + suffix);
            std::filesystem::create_directories(dir_);
        }
        ~ScopedDir()
        {
            std::error_code ec;
            std::filesystem::remove_all(dir_, ec);
        }
        std::filesystem::path Write(const std::string& name, const std::string& contents) const
        {
            const std::filesystem::path p = dir_ / name;
            std::ofstream out(p, std::ios::binary | std::ios::trunc);
            REQUIRE(out.is_open());
            out << contents;
            return p;
        }

    private:
        std::filesystem::path dir_;
    };
}

TEST_CASE("Scad lexer tokenizes primitives and keyword arguments", "[Unit][Scad]")
{
    std::vector<Token> tokens;
    std::string err;
    REQUIRE(ScadLexer::Tokenize("cube([1, 2.5, 3], center=true); // comment\n", tokens, err));

    REQUIRE(tokens.size() >= 12);
    CHECK(tokens[0].kind == Tok::Ident);
    CHECK(tokens[0].text == "cube");
    CHECK(tokens[1].kind == Tok::LParen);
    CHECK(tokens[2].kind == Tok::LBracket);
    CHECK(tokens[3].kind == Tok::Number);
    CHECK(tokens[3].number == Catch::Approx(1.0));
    CHECK(tokens.back().kind == Tok::Eof);
}

TEST_CASE("Scad lexer reads special variables", "[Unit][Scad]")
{
    std::vector<Token> tokens;
    std::string err;
    REQUIRE(ScadLexer::Tokenize("$fn = 32;", tokens, err));
    CHECK(tokens[0].kind == Tok::Special);
    CHECK(tokens[0].text == "$fn");
}

TEST_CASE("Scad parser builds module, function and for AST", "[Unit][Scad]")
{
    std::vector<Token> tokens;
    std::string err;
    REQUIRE(ScadLexer::Tokenize(
        "function f(x) = x * 2;\n"
        "module m(n=3) { for (i = [0:n-1]) cube(1); }\n"
        "m();\n",
        tokens, err));

    Scope scope;
    REQUIRE(ScadParser::Parse(tokens, scope, err));
    REQUIRE(scope.size() == 3);
    CHECK(scope[0]->kind == StmtKind::FunctionDef);
    CHECK(scope[0]->name == "f");
    CHECK(scope[1]->kind == StmtKind::ModuleDef);
    CHECK(scope[1]->name == "m");
    REQUIRE(scope[1]->params.size() == 1);
    CHECK(scope[1]->params[0].name == "n");
    CHECK(scope[2]->kind == StmtKind::Instance);
    CHECK(scope[2]->name == "m");
}

TEST_CASE("Scad evaluator emits a centered cube", "[Unit][Scad]")
{
    const EvalResult result = EvalProgram("cube([2,2,2], center=true);");
    CHECK(TotalTriangles(result) == 12); // 6 quads * 2
    CHECK(result.buckets.size() == 1);
}

TEST_CASE("Scad evaluator resolves user modules, functions and for-loops", "[Unit][Scad]")
{
    const EvalResult result = EvalProgram(
        "function side() = 1;\n"
        "module unit() { cube(side()); }\n"
        "for (i = [0:3]) translate([i*2, 0, 0]) unit();\n");
    // 4 iterations * one cube each = 48 triangles.
    CHECK(TotalTriangles(result) == 48);
}

TEST_CASE("Scad difference subtracts when a CSG backend is present", "[Unit][Scad]")
{
    // Inner cube fully contained -> hollow shell when the boolean backend runs;
    // first-child approximation (no backend) leaves just the outer cube.
    const EvalResult result = EvalProgram(
        "difference() {\n"
        "  cube([4,4,4], center=true);\n"
        "  cube([2,2,2], center=true);\n"
        "}\n");
    CHECK(result.buckets.size() == 1);
    if (ScadCsg::BackendAvailable())
    {
        CHECK(TotalTriangles(result) > 12); // a real cavity adds geometry
    }
    else
    {
        CHECK(TotalTriangles(result) == 12); // degraded: outer cube only
    }
}

TEST_CASE("Scad difference cuts a through-hole with the Manifold backend", "[Unit][Scad]")
{
    if (!ScadCsg::BackendAvailable())
    {
        return;
    }
    // A box with a slab punched all the way through must stay non-empty and
    // gain geometry relative to the solid box (12 tris).
    const EvalResult result = EvalProgram(
        "difference() {\n"
        "  cube([4,4,4], center=true);\n"
        "  cube([10,2,2], center=true);\n"
        "}\n");
    CHECK(result.buckets.size() == 1);
    CHECK(TotalTriangles(result) > 12);
}

TEST_CASE("Scad color groups split geometry into buckets", "[Unit][Scad]")
{
    const EvalResult result = EvalProgram(
        "color([1,0,0]) cube(1);\n"
        "color([0,1,0]) translate([2,0,0]) cube(1);\n");
    CHECK(result.buckets.size() == 2);
    CHECK(TotalTriangles(result) == 24);
}

TEST_CASE("Scad linear_extrude of a triangle polygon builds a prism", "[Unit][Scad]")
{
    const EvalResult result = EvalProgram(
        "linear_extrude(1) polygon(points=[[0,0],[2,0],[0,2]]);\n");
    // triangle: top + bottom caps (1 each) + 3 side walls (2 each) = 8 triangles.
    CHECK(TotalTriangles(result) == 8);
}

TEST_CASE("Scad linear_extrude of text builds glyph geometry when FreeType is present", "[Unit][Scad]")
{
    if (!ScadText::Available())
    {
        WARN("FreeType backend unavailable; skipping text() geometry test");
        return;
    }
    // ASCII keeps the test font-independent; CJK works too via DroidSansFallback.
    const EvalResult ascii = EvalProgram("linear_extrude(1) text(\"AB\", size=10);\n");
    CHECK(ascii.buckets.size() == 1);
    CHECK(TotalTriangles(ascii) > 0);

    const EvalResult cjk = EvalProgram("linear_extrude(0.1) text(\"\xE9\x85\x92\xE6\xA5\xBC\", size=2.2, halign=\"center\", valign=\"center\");\n");
    CHECK(TotalTriangles(cjk) > 0); // 酒楼
}

TEST_CASE("Scad rotate_extrude revolves a 2D profile into a solid", "[Unit][Scad]")
{
    const EvalResult result = EvalProgram(
        "rotate_extrude(angle=360) translate([10,0,0]) circle(r=2);\n");
    CHECK(result.buckets.size() == 1);
    CHECK(TotalTriangles(result) > 0); // a torus surface
}

TEST_CASE("Scad translucent color() becomes a dielectric material", "[Unit][Scad]")
{
    ScopedDir dir;
    const std::filesystem::path mainPath = dir.Write("glass.scad",
              "color([0.8,0.9,1.0,0.3]) cube([2,2,2]);\n");

    Assets::EnvironmentSetting environment;
    std::vector<std::shared_ptr<Assets::Node>> nodes;
    std::vector<Assets::Model> models;
    std::vector<Assets::FMaterial> materials;
    std::vector<Assets::LightObject> lights;
    std::vector<Assets::AnimationTrack> tracks;
    std::vector<Assets::Skeleton> skeletons;

    REQUIRE(Assets::FScadLoader::LoadScadScene(
        mainPath.string(), environment, nodes, models, materials, lights, tracks, skeletons));

    REQUIRE(materials.size() == 1);
    CHECK(materials[0].gpuMaterial_.MaterialModel == Assets::Material::Enum::Dielectric);
    CHECK(materials[0].gpuMaterial_.Diffuse.a == Catch::Approx(0.3f).margin(1e-3f));
}

TEST_CASE("Scad loader: loads the bundled beer_cup sample when present", "[Unit][Scad]")
{
    const std::filesystem::path samplePath =
        std::filesystem::path(Utilities::FileHelper::GetPlatformFilePath("assets/scad/beer_cup.scad"));
    if (!std::filesystem::exists(samplePath))
    {
        WARN("assets/scad/beer_cup.scad not present; skipping sample load");
        return;
    }

    Assets::EnvironmentSetting environment;
    std::vector<std::shared_ptr<Assets::Node>> nodes;
    std::vector<Assets::Model> models;
    std::vector<Assets::FMaterial> materials;
    std::vector<Assets::LightObject> lights;
    std::vector<Assets::AnimationTrack> tracks;
    std::vector<Assets::Skeleton> skeletons;

    REQUIRE(Assets::FScadLoader::LoadScadScene(
        "assets/scad/beer_cup.scad",
        environment, nodes, models, materials, lights, tracks, skeletons));

    CHECK(nodes.size() > 1);   // glass, beer, foam, bubbles, handle ...
    CHECK(models.size() > 1);
    // At least one translucent (dielectric) material for the glass/liquid.
    bool anyDielectric = false;
    for (const auto& m : materials)
    {
        if (m.gpuMaterial_.MaterialModel == Assets::Material::Enum::Dielectric) anyDielectric = true;
    }
    CHECK(anyDielectric);
}

TEST_CASE("Scad user module children() forwards child geometry", "[Unit][Scad]")
{
    const EvalResult all = EvalProgram(
        "module wrap() { translate([0,0,1]) children(); }\n"
        "wrap() cube([2,2,2]);\n");
    CHECK(all.buckets.size() == 1);
    CHECK(TotalTriangles(all) == 12); // the single cube, forwarded

    const EvalResult pick = EvalProgram(
        "module pick() { children(0); }\n"
        "pick() { cube([1,1,1]); cube([2,2,2]); }\n");
    CHECK(TotalTriangles(pick) == 12); // only child 0
}

TEST_CASE("Scad list comprehension drives a for-loop", "[Unit][Scad]")
{
    const EvalResult gen = EvalProgram(
        "for (x = [for (i = [1:3]) i * 2]) translate([x,0,0]) cube([1,1,1]);\n");
    CHECK(TotalTriangles(gen) == 36); // 3 cubes

    const EvalResult filtered = EvalProgram(
        "for (x = [for (i = [0:5]) if (i % 2 == 0) i]) translate([x,0,0]) cube([1,1,1]);\n");
    CHECK(TotalTriangles(filtered) == 36); // i = 0,2,4 -> 3 cubes

    const EvalResult eached = EvalProgram(
        "for (x = [each [0, 5], for (i = [1:2]) i]) translate([x,0,0]) cube([1,1,1]);\n");
    CHECK(TotalTriangles(eached) == 48); // 0,5,1,2 -> 4 cubes
}

TEST_CASE("Scad echo / assert / str are accepted", "[Unit][Scad]")
{
    const EvalResult result = EvalProgram(
        "echo(str(\"size=\", 5));\n"
        "assert(1 == 1);\n"
        "cube([1,1,1]);\n");
    CHECK(TotalTriangles(result) == 12);
}

TEST_CASE("Scad loader: use imports definitions without executing previews", "[Unit][Scad]")
{
    ScopedDir dir;
    dir.Write("lib.scad",
              "module box1() { cube([1,1,1]); }\n"
              "box1(); // standalone preview, must be ignored when use'd\n");
    const std::filesystem::path mainPath = dir.Write("main.scad",
              "use <lib.scad>\n"
              "box1();\n");

    Assets::EnvironmentSetting environment;
    std::vector<std::shared_ptr<Assets::Node>> nodes;
    std::vector<Assets::Model> models;
    std::vector<Assets::FMaterial> materials;
    std::vector<Assets::LightObject> lights;
    std::vector<Assets::AnimationTrack> tracks;
    std::vector<Assets::Skeleton> skeletons;

    REQUIRE(Assets::FScadLoader::LoadScadScene(
        mainPath.string(), environment, nodes, models, materials, lights, tracks, skeletons));

    REQUIRE(nodes.size() == 1);
    REQUIRE(models.size() == 1);
    REQUIRE(materials.size() == 1);
    // A single cube = 12 triangles = 36 vertices; preview was not executed.
    CHECK(models[0].NumberOfVertices() == 36);

    auto render = nodes[0]->GetComponent<Runtime::RenderComponent>();
    REQUIRE(render);
    CHECK(render->GetModelId() == 0);
}

TEST_CASE("Scad loader: Z-up converts to engine Y-up", "[Unit][Scad]")
{
    ScopedDir dir;
    // A unit cube spanning z in [0,1] should occupy y in [0,1] after conversion.
    const std::filesystem::path mainPath = dir.Write("h.scad", "cube([1,1,1]);\n");

    Assets::EnvironmentSetting environment;
    std::vector<std::shared_ptr<Assets::Node>> nodes;
    std::vector<Assets::Model> models;
    std::vector<Assets::FMaterial> materials;
    std::vector<Assets::LightObject> lights;
    std::vector<Assets::AnimationTrack> tracks;
    std::vector<Assets::Skeleton> skeletons;

    REQUIRE(Assets::FScadLoader::LoadScadScene(
        mainPath.string(), environment, nodes, models, materials, lights, tracks, skeletons));

    REQUIRE(models.size() == 1);
    CHECK(models[0].GetLocalAABBMin().y == Catch::Approx(0.0f).margin(1e-5f));
    CHECK(models[0].GetLocalAABBMax().y == Catch::Approx(1.0f).margin(1e-5f));
}

TEST_CASE("Scad loader: loads the bundled ancient_city sample when present", "[Unit][Scad]")
{
    const std::filesystem::path samplePath =
        std::filesystem::path(Utilities::FileHelper::GetPlatformFilePath("assets/scad/acient_city.scad"));
    if (!std::filesystem::exists(samplePath))
    {
        WARN("assets/scad/acient_city.scad not present in runtime root; skipping sample load");
        return;
    }

    Assets::EnvironmentSetting environment;
    std::vector<std::shared_ptr<Assets::Node>> nodes;
    std::vector<Assets::Model> models;
    std::vector<Assets::FMaterial> materials;
    std::vector<Assets::LightObject> lights;
    std::vector<Assets::AnimationTrack> tracks;
    std::vector<Assets::Skeleton> skeletons;

    REQUIRE(Assets::FScadLoader::LoadScadScene(
        "assets/scad/acient_city.scad",
        environment, nodes, models, materials, lights, tracks, skeletons));

    CHECK(nodes.size() > 1);
    CHECK(models.size() > 1);
    CHECK(materials.size() > 1);
}
