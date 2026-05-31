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
#include <limits>
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

TEST_CASE("Scad evaluator resolves local module and function definitions", "[Unit][Scad]")
{
    const EvalResult result = EvalProgram(
        "module outer() {\n"
        "  marker(3);\n"
        "  function y(x) = x + 2;\n"
        "  module marker(x) { translate([0, y(x), 0]) cube([1,1,1]); }\n"
        "}\n"
        "outer();\n");

    CHECK(result.warningCount == 0);
    CHECK(TotalTriangles(result) == 12);

    double minY = std::numeric_limits<double>::max();
    double maxY = std::numeric_limits<double>::lowest();
    for (const auto& entry : result.buckets)
    {
        for (const glm::dvec3& p : entry.second.tris)
        {
            minY = std::min(minY, p.y);
            maxY = std::max(maxY, p.y);
        }
    }
    CHECK(minY == Catch::Approx(5.0).margin(1e-6));
    CHECK(maxY == Catch::Approx(6.0).margin(1e-6));
}

TEST_CASE("Scad evaluator handles generated city helpers nested inside a module", "[Unit][Scad]")
{
    const EvalResult result = EvalProgram(
        "$fn = 16;\n"
        "module city() {\n"
        "  function wave_y(x) = -14 + 5 * sin((x + 50) * 3.2);\n"
        "  module _tree(x, y, s=1) {\n"
        "    color([0.42,0.22,0.08]) translate([x, y, 0.9*s]) cylinder(r=0.16*s, h=1.8*s, center=true);\n"
        "    color([0.05,0.45,0.16]) translate([x, y, 2.0*s]) sphere(r=0.72*s);\n"
        "  }\n"
        "  color([0.72,0.74,0.69]) translate([0, 0, -0.08]) cube([100, 80, 0.16], center=true);\n"
        "  color([0.08,0.38,0.78,0.68]) translate([0,0,0.18]) linear_extrude(height=0.12)\n"
        "    polygon(points = concat(\n"
        "      [for (x = [-50:10:50]) [x, wave_y(x)+3.2]],\n"
        "      [for (x = [50:-10:-50]) [x, wave_y(x)-3.2]]\n"
        "    ));\n"
        "  for (x = [-48:8:48]) {\n"
        "    _tree(x, wave_y(x)+5.2, 0.55);\n"
        "    _tree(x+3, wave_y(x)-5.0, 0.5);\n"
        "  }\n"
        "}\n"
        "city();\n");

    CHECK(result.warningCount == 0);
    CHECK(TotalTriangles(result) > 12);

    const ColorBucket* river = nullptr;
    for (const auto& entry : result.buckets)
    {
        const glm::vec4& c = entry.second.color;
        if (c.r == Catch::Approx(0.08f).margin(1e-3f) && c.b == Catch::Approx(0.78f).margin(1e-3f) &&
            c.a == Catch::Approx(0.68f).margin(1e-3f))
        {
            river = &entry.second;
            break;
        }
    }
    REQUIRE(river != nullptr);

    double minY = std::numeric_limits<double>::max();
    double maxY = std::numeric_limits<double>::lowest();
    for (const glm::dvec3& p : river->tris)
    {
        minY = std::min(minY, p.y);
        maxY = std::max(maxY, p.y);
    }
    CHECK((maxY - minY) > 8.0);
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

TEST_CASE("Scad difference hollows a unioned tapered cup shell", "[Unit][Scad]")
{
    if (!ScadCsg::BackendAvailable())
    {
        return;
    }

    const EvalResult result = EvalProgram(
        "$fn = 96;\n"
        "difference() {\n"
        "  union() {\n"
        "    cylinder(h = 90, r1 = 28, r2 = 28 * 0.95);\n"
        "    cylinder(h = 8, r = 28 * 1.02);\n"
        "  }\n"
        "  translate([0, 0, 8]) cylinder(h = 91, r1 = 24, r2 = 24 * 0.95);\n"
        "}\n");

    REQUIRE(result.buckets.size() == 1);
    const auto& tris = result.buckets.begin()->second.tris;
    bool hasTopCenterCapVertex = false;
    bool hasTopInnerRimVertex = false;
    bool hasBottomInnerRimVertex = false;
    for (const glm::dvec3& p : tris)
    {
        const double r = std::sqrt(p.x * p.x + p.y * p.y);
        if (std::abs(p.z - 90.0) < 1e-5 && r < 1.0)
        {
            hasTopCenterCapVertex = true;
        }
        if (std::abs(p.z - 90.0) < 1e-5 && r > 22.0 && r < 24.5)
        {
            hasTopInnerRimVertex = true;
        }
        if (std::abs(p.z - 8.0) < 1e-5 && r > 23.5 && r < 24.5)
        {
            hasBottomInnerRimVertex = true;
        }
    }
    CHECK_FALSE(hasTopCenterCapVertex);
    CHECK(hasTopInnerRimVertex);
    CHECK(hasBottomInnerRimVertex);
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

TEST_CASE("Scad loader: recreates module hierarchy with module names", "[Unit][Scad]")
{
    ScopedDir dir;
    const std::filesystem::path mainPath = dir.Write("main.scad",
              "module helper() { cube([1,1,1]); }\n"
              "module house() { helper(); }\n"
              "module roof() { translate([3,0,0]) cube([1,1,1]); }\n"
              "color([1,0,0]) house();\n"
              "color([1,0,0]) roof();\n");

    Assets::EnvironmentSetting environment;
    std::vector<std::shared_ptr<Assets::Node>> nodes;
    std::vector<Assets::Model> models;
    std::vector<Assets::FMaterial> materials;
    std::vector<Assets::LightObject> lights;
    std::vector<Assets::AnimationTrack> tracks;
    std::vector<Assets::Skeleton> skeletons;

    REQUIRE(Assets::FScadLoader::LoadScadScene(
        mainPath.string(), environment, nodes, models, materials, lights, tracks, skeletons));

    REQUIRE(nodes.size() == 3);
    Assets::Node* house = nullptr;
    Assets::Node* helper = nullptr;
    Assets::Node* roof = nullptr;
    for (const std::shared_ptr<Assets::Node>& node : nodes)
    {
        if (node->GetName() == "house")
        {
            house = node.get();
        }
        else if (node->GetName() == "helper")
        {
            helper = node.get();
        }
        else if (node->GetName() == "roof")
        {
            roof = node.get();
        }
    }
    REQUIRE(house);
    REQUIRE(helper);
    REQUIRE(roof);
    CHECK(house->GetParent() == nullptr);
    CHECK(helper->GetParent() == house);
    CHECK(roof->GetParent() == nullptr);
}

TEST_CASE("Scad loader: keeps repeated leaf-module instances as separate nodes", "[Unit][Scad]")
{
    ScopedDir dir;
    const std::filesystem::path mainPath = dir.Write("main.scad",
              "module panel() { cube([1,1,1]); }\n"
              "for (x = [0, 2]) translate([x,0,0]) panel();\n");

    Assets::EnvironmentSetting environment;
    std::vector<std::shared_ptr<Assets::Node>> nodes;
    std::vector<Assets::Model> models;
    std::vector<Assets::FMaterial> materials;
    std::vector<Assets::LightObject> lights;
    std::vector<Assets::AnimationTrack> tracks;
    std::vector<Assets::Skeleton> skeletons;

    REQUIRE(Assets::FScadLoader::LoadScadScene(
        mainPath.string(), environment, nodes, models, materials, lights, tracks, skeletons));

    REQUIRE(nodes.size() == 2);
    CHECK(nodes[0]->GetName() == "panel");
    CHECK(nodes[1]->GetName() == "panel");
    CHECK(nodes[0]->GetParent() == nullptr);
    CHECK(nodes[1]->GetParent() == nullptr);
}

TEST_CASE("Scad loader: merges direct mesh buckets under one module node", "[Unit][Scad]")
{
    ScopedDir dir;
    const std::filesystem::path mainPath = dir.Write("main.scad",
              "module facade() {\n"
              "  color([1,0,0]) cube([1,1,1]);\n"
              "  color([0,1,0]) translate([2,0,0]) cube([1,1,1]);\n"
              "}\n"
              "facade();\n");

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
    CHECK(nodes[0]->GetName() == "facade");

    auto render = nodes[0]->GetComponent<Runtime::RenderComponent>();
    REQUIRE(render);
    REQUIRE(models.size() == 1);
    REQUIRE(materials.size() == 2);
    CHECK(render->GetModelId() == 0);
    CHECK(models[0].SectionCount() == 2);
}

TEST_CASE("Scad loader: nested module calls keep translated child transforms", "[Unit][Scad]")
{
    ScopedDir dir;
    const std::filesystem::path mainPath = dir.Write("main.scad",
              "module leaf() { cube([1,1,1], center=true); }\n"
              "module parent() { translate([0,-10,0]) leaf(); }\n"
              "parent();\n");

    Assets::EnvironmentSetting environment;
    std::vector<std::shared_ptr<Assets::Node>> nodes;
    std::vector<Assets::Model> models;
    std::vector<Assets::FMaterial> materials;
    std::vector<Assets::LightObject> lights;
    std::vector<Assets::AnimationTrack> tracks;
    std::vector<Assets::Skeleton> skeletons;

    REQUIRE(Assets::FScadLoader::LoadScadScene(
        mainPath.string(), environment, nodes, models, materials, lights, tracks, skeletons));

    Assets::Node* leaf = nullptr;
    for (const std::shared_ptr<Assets::Node>& node : nodes)
    {
        if (node->GetName() == "leaf")
        {
            leaf = node.get();
            break;
        }
    }

    REQUIRE(leaf);
    CHECK(leaf->WorldTranslation().z == Catch::Approx(10.0f).margin(1e-4f));
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

    glm::vec3 sceneMin(std::numeric_limits<float>::max());
    glm::vec3 sceneMax(std::numeric_limits<float>::lowest());
    for (const std::shared_ptr<Assets::Node>& node : nodes)
    {
        auto render = node->GetComponent<Runtime::RenderComponent>();
        if (!render)
        {
            continue;
        }

        const Assets::Model& model = models[render->GetModelId()];
        const glm::mat4 world = node->WorldTransform();
        for (const Assets::Vertex& vertex : model.CPUVertices())
        {
            const glm::vec3 p = world * glm::vec4(vertex.Position, 1.0f);
            sceneMin = glm::min(sceneMin, p);
            sceneMax = glm::max(sceneMax, p);
        }
    }
    CHECK((sceneMax.y - sceneMin.y) < 0.12f);
    CHECK((sceneMax.x - sceneMin.x) < 0.12f);
    CHECK((sceneMax.z - sceneMin.z) < 0.12f);
}

TEST_CASE("Scad loader: bundled beer_cup glass body is hollow", "[Unit][Scad]")
{
    if (!ScadCsg::BackendAvailable())
    {
        return;
    }

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

    int glassBodyIndex = -1;
    for (size_t i = 0; i < materials.size(); ++i)
    {
        const glm::vec4 diffuse = materials[i].gpuMaterial_.Diffuse;
        if (diffuse.a == Catch::Approx(0.28f).margin(1e-3f))
        {
            glassBodyIndex = static_cast<int>(i);
            break;
        }
    }
    REQUIRE(glassBodyIndex >= 0);
    REQUIRE(static_cast<size_t>(glassBodyIndex) < models.size());

    const Assets::Model& glassBody = models[static_cast<size_t>(glassBodyIndex)];
    bool hasTopCenterCapVertex = false;
    bool hasTopInnerRimVertex = false;
    for (const Assets::Vertex& v : glassBody.CPUVertices())
    {
        const glm::vec3 p = v.Position;
        // Loader converts SCAD (x,y,z) to engine (x,z,-y), so cup top z=0.09m is y=0.09m.
        const float radial = std::sqrt(p.x * p.x + p.z * p.z);
        if (std::abs(p.y - 0.09f) < 1e-6f && radial < 0.001f)
        {
            hasTopCenterCapVertex = true;
        }
        if (std::abs(p.y - 0.09f) < 1e-6f && radial > 0.022f && radial < 0.0245f)
        {
            hasTopInnerRimVertex = true;
        }
    }
    CHECK_FALSE(hasTopCenterCapVertex);
    CHECK(hasTopInnerRimVertex);
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

TEST_CASE("Scad negative-determinant transforms keep outward winding", "[Unit][Scad]")
{
    const EvalResult result = EvalProgram("mirror([1,0,0]) cube([1,1,1]);\n");
    REQUIRE(result.buckets.size() == 1);

    const auto& tris = result.buckets.begin()->second.tris;
    REQUIRE(tris.size() == 36);
    const glm::dvec3 center(-0.5, 0.5, 0.5);
    for (size_t i = 0; i + 2 < tris.size(); i += 3)
    {
        const glm::dvec3 normal = glm::cross(tris[i + 1] - tris[i + 0], tris[i + 2] - tris[i + 0]);
        const glm::dvec3 centroid = (tris[i + 0] + tris[i + 1] + tris[i + 2]) / 3.0;
        CHECK(glm::dot(normal, centroid - center) > 0.0);
    }
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

TEST_CASE("Scad loader: comments and strings do not create use/include directives", "[Unit][Scad]")
{
    ScopedDir dir;
    const std::filesystem::path mainPath = dir.Write("main.scad",
              "echo(\"use <missing_from_string.scad>\");\n"
              "// include <missing_from_comment.scad>\n"
              "/* use <missing_from_block_comment.scad> */\n"
              "cube([1,1,1]);\n");

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
    CHECK(models[0].NumberOfVertices() == 36);
}

TEST_CASE("Scad loader: include executes a file even if use imported it first", "[Unit][Scad]")
{
    ScopedDir dir;
    dir.Write("lib.scad",
              "module box1() { cube([1,1,1]); }\n"
              "box1();\n");
    const std::filesystem::path mainPath = dir.Write("main.scad",
              "use <lib.scad>\n"
              "include <lib.scad>\n"
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
    REQUIRE(nodes.size() == 2);
    REQUIRE(models.size() == 2);
    CHECK(nodes[0]->GetName() == "box1");
    CHECK(nodes[1]->GetName() == "box1");
    CHECK(models[0].NumberOfVertices() == 36);
    CHECK(models[1].NumberOfVertices() == 36);
}

TEST_CASE("Scad loader: parse errors fail the load", "[Unit][Scad]")
{
    ScopedDir dir;
    const std::filesystem::path mainPath = dir.Write("bad.scad", "cube([1, 2,\n");

    Assets::EnvironmentSetting environment;
    std::vector<std::shared_ptr<Assets::Node>> nodes;
    std::vector<Assets::Model> models;
    std::vector<Assets::FMaterial> materials;
    std::vector<Assets::LightObject> lights;
    std::vector<Assets::AnimationTrack> tracks;
    std::vector<Assets::Skeleton> skeletons;

    CHECK_FALSE(Assets::FScadLoader::LoadScadScene(
        mainPath.string(), environment, nodes, models, materials, lights, tracks, skeletons));
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

    glm::vec3 sceneMin(std::numeric_limits<float>::max());
    glm::vec3 sceneMax(std::numeric_limits<float>::lowest());
    for (const Assets::Model& model : models)
    {
        sceneMin = glm::min(sceneMin, model.GetLocalAABBMin());
        sceneMax = glm::max(sceneMax, model.GetLocalAABBMax());
    }

    const glm::vec3 extent = sceneMax - sceneMin;
    CHECK(extent.x == Catch::Approx(330.0f).margin(2.0f));
    CHECK(extent.z == Catch::Approx(260.0f).margin(2.0f));
    CHECK(extent.y > 30.0f);

    REQUIRE_FALSE(environment.cameras.empty());
    const Assets::Camera& camera = environment.cameras[0];
    const glm::vec3 center = (sceneMin + sceneMax) * 0.5f;
    const float radius = glm::length(extent) * 0.5f;
    const glm::vec3 eye = glm::vec3(glm::inverse(camera.ModelView)[3]);
    const float cameraDistance = glm::distance(eye, center);
    CHECK(camera.FarPlane > cameraDistance + radius);
    CHECK(camera.NearPlane < cameraDistance - radius);
}
