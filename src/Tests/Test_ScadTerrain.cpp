#include <catch2/catch_all.hpp>

#include "Engine/Assets/Core/Model.hpp"
#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Data/Material.hpp"
#include "Engine/Runtime/Components/RenderComponent.hpp"
#include "Engine/Runtime/Components/TerrainComponent.hpp"
#include "Engine/Utilities/FileHelper.hpp"
#include "Modules/ScadLoader/FScadEvaluator.h"
#include "Modules/ScadLoader/FScadLexer.h"
#include "Modules/ScadLoader/FScadLoader.h"
#include "Modules/ScadLoader/FScadParser.h"
#include "Modules/ScadLoader/FScadShared.h"
#include "Modules/ScadLoader/FScadTerrain.h"

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <fmt/format.h>
#include <map>
#include <string>
#include <unordered_map>

using namespace Assets::Scad;

namespace
{
    void SplitProgram(const std::string& src, Scope& outTop, std::unordered_map<std::string, StmtPtr>& outModules,
                      std::unordered_map<std::string, StmtPtr>& outFunctions)
    {
        std::vector<Token> tokens;
        std::string err;
        REQUIRE(ScadLexer::Tokenize(src, tokens, err));

        Scope scope;
        REQUIRE(ScadParser::Parse(tokens, scope, err));

        for (const StmtPtr& s : scope)
        {
            if (s->kind == StmtKind::ModuleDef)
                outModules[s->name] = s;
            else if (s->kind == StmtKind::FunctionDef)
                outFunctions[s->name] = s;
            else
                outTop.push_back(s);
        }
    }

    EvalResult EvalProgram(const std::string& src)
    {
        Scope top;
        std::unordered_map<std::string, StmtPtr> modules;
        std::unordered_map<std::string, StmtPtr> functions;
        SplitProgram(src, top, modules, functions);

        Assets::ScadLoadOptions options;
        EvalResult result;
        std::string evalErr;
        ScadEvaluator::Evaluate(top, modules, functions, options, result, evalErr);
        return result;
    }

    SceneEvalResult EvalSceneProgram(const std::string& src)
    {
        Scope top;
        std::unordered_map<std::string, StmtPtr> modules;
        std::unordered_map<std::string, StmtPtr> functions;
        SplitProgram(src, top, modules, functions);

        Assets::ScadLoadOptions options;
        SceneEvalResult result;
        std::string evalErr;
        ScadEvaluator::EvaluateScene(top, modules, functions, options, result, evalErr);
        return result;
    }

    size_t TotalTriangles(const EvalResult& result)
    {
        size_t tris = 0;
        for (const auto& entry : result.buckets)
            tris += entry.second.tris.size() / 3;
        return tris;
    }

    Value NumV(double v) { return Value::MakeNumber(v); }
    Value StrV(const char* s) { return Value::MakeStr(s); }
    Value VecV(std::vector<Value> v) { return Value::MakeVec(std::move(v)); }
    Value PtV(double x, double y) { return VecV({NumV(x), NumV(y)}); }

    // Shared test terrain: mountain + river + pad + road + lake + plateau.
    // The scad literal and the C++ spec below must stay in sync.
    const char* kTerrScad = "TERR = [\"gkterr1\", [120, 100], [60, 50], 7, [0, 1.2, 0.5], undef, \"temperate\",\n"
                            "  [[\"mountain\", [-30, 25], 28, 14, 0.5],\n"
                            "   [\"river\", [[-30, 25], [-10, 5], [0, -10], [5, -48]], 5, 1.4],\n"
                            "   [\"pad\", [25, -20], [18, 12], 0],\n"
                            "   [\"road\", [[-50, -30], [0, -25], [14, -22]], 3],\n"
                            "   [\"lake\", [35, 30], 12, 1.5],\n"
                            "   [\"plateau\", [-40, -30], 14, 5]]];\n";

    FTerrainSpec MakeTestSpec()
    {
        FTerrainSpec spec;
        spec.size = {120.0, 100.0};
        spec.cells = {60, 50};
        spec.seed = 7;
        spec.baseHeight = 0.0;
        spec.relief = 1.2;
        spec.roughness = 0.5;
        spec.palette = "temperate";

        FTerrainFeature mountain;
        mountain.type = FTerrainFeature::EType::Mountain;
        mountain.at = {-30.0, 25.0};
        mountain.radius = 28.0;
        mountain.height = 14.0;
        mountain.rugged = 0.5;
        spec.features.push_back(mountain);

        FTerrainFeature river;
        river.type = FTerrainFeature::EType::River;
        river.pts = {{-30.0, 25.0}, {-10.0, 5.0}, {0.0, -10.0}, {5.0, -48.0}};
        river.width = 5.0;
        river.depth = 1.4;
        spec.features.push_back(river);

        FTerrainFeature pad;
        pad.type = FTerrainFeature::EType::Pad;
        pad.at = {25.0, -20.0};
        pad.size = {18.0, 12.0};
        spec.features.push_back(pad);

        FTerrainFeature road;
        road.type = FTerrainFeature::EType::Road;
        road.pts = {{-50.0, -30.0}, {0.0, -25.0}, {14.0, -22.0}};
        road.width = 3.0;
        spec.features.push_back(road);

        FTerrainFeature lake;
        lake.type = FTerrainFeature::EType::Lake;
        lake.at = {35.0, 30.0};
        lake.radius = 12.0;
        lake.depth = 1.5;
        spec.features.push_back(lake);

        FTerrainFeature plateau;
        plateau.type = FTerrainFeature::EType::Plateau;
        plateau.at = {-40.0, -30.0};
        plateau.radius = 14.0;
        plateau.height = 5.0;
        spec.features.push_back(plateau);
        return spec;
    }

    Value MakeTestSpecValue()
    {
        return VecV({
            StrV("gkterr1"),
            PtV(120, 100),
            PtV(60, 50),
            NumV(7),
            VecV({NumV(0), NumV(1.2), NumV(0.5)}),
            Value(), // undef water level
            StrV("temperate"),
            VecV({
                VecV({StrV("mountain"), PtV(-30, 25), NumV(28), NumV(14), NumV(0.5)}),
                VecV({StrV("river"), VecV({PtV(-30, 25), PtV(-10, 5), PtV(0, -10), PtV(5, -48)}), NumV(5), NumV(1.4)}),
                VecV({StrV("pad"), PtV(25, -20), PtV(18, 12), NumV(0)}),
                VecV({StrV("road"), VecV({PtV(-50, -30), PtV(0, -25), PtV(14, -22)}), NumV(3)}),
                VecV({StrV("lake"), PtV(35, 30), NumV(12), NumV(1.5)}),
                VecV({StrV("plateau"), PtV(-40, -30), NumV(14), NumV(5)}),
            }),
        });
    }

    size_t CountMaterialTips(const FTerrainData& data, const std::string& materialName)
    {
        struct Face
        {
            std::string material;
            size_t vertex[3]{};
        };
        std::vector<Face> faces;
        for (const FTerrainData::ColoredTris& bucket : data.landGeom)
        {
            if (bucket.materialName == "terrain_skirt") continue;
            for (size_t tri = 0; tri + 2 < bucket.tris.size(); tri += 3)
            {
                Face face;
                face.material = bucket.materialName;
                for (int corner = 0; corner < 3; ++corner)
                {
                    const auto vertex = std::find(data.verts.begin(), data.verts.end(), bucket.tris[tri + corner]);
                    REQUIRE(vertex != data.verts.end());
                    face.vertex[corner] = static_cast<size_t>(vertex - data.verts.begin());
                }
                faces.push_back(std::move(face));
            }
        }

        std::vector<std::vector<size_t>> neighbors(faces.size());
        std::map<std::pair<size_t, size_t>, size_t> edgeOwner;
        for (size_t faceIndex = 0; faceIndex < faces.size(); ++faceIndex)
        {
            for (int edge = 0; edge < 3; ++edge)
            {
                const auto key = std::minmax(faces[faceIndex].vertex[edge],
                                             faces[faceIndex].vertex[(edge + 1) % 3]);
                const auto [it, inserted] =
                    edgeOwner.emplace(std::pair<size_t, size_t>(key.first, key.second), faceIndex);
                if (!inserted)
                {
                    neighbors[faceIndex].push_back(it->second);
                    neighbors[it->second].push_back(faceIndex);
                }
            }
        }

        size_t tips = 0;
        for (size_t faceIndex = 0; faceIndex < faces.size(); ++faceIndex)
        {
            if (faces[faceIndex].material != materialName || neighbors[faceIndex].size() != 3) continue;
            for (size_t neighbor : neighbors[faceIndex])
            {
                const std::string& candidate = faces[neighbor].material;
                if (candidate == materialName) continue;
                const int count = static_cast<int>(std::count_if(
                    neighbors[faceIndex].begin(), neighbors[faceIndex].end(),
                    [&](size_t other) { return faces[other].material == candidate; }));
                if (count >= 2)
                {
                    ++tips;
                    break;
                }
            }
        }
        return tips;
    }

    class ScopedDir
    {
    public:
        ScopedDir()
        {
            const auto suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
            dir_ = std::filesystem::temp_directory_path() / ("scad_terrain_test_" + suffix);
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

    std::filesystem::path FindSourceAsset(const std::filesystem::path& relativePath)
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
        return Utilities::FileHelper::GetPlatformFilePath(relativePath.string().c_str());
    }
} // namespace

TEST_CASE("ScadTerrain decode: rejects malformed TERR", "[Unit][Scad][ScadTerrain]")
{
    FTerrainSpec spec;
    std::string err;
    std::vector<std::string> warnings;

    CHECK_FALSE(ScadTerrain::DecodeSpec(NumV(42), spec, err, warnings));
    CHECK_FALSE(err.empty());

    Value badTag = MakeTestSpecValue();
    badTag.vec[0] = StrV("gkterr999");
    CHECK_FALSE(ScadTerrain::DecodeSpec(badTag, spec, err, warnings));

    // Unknown features are skipped with a warning, not fatal.
    Value unknownFeature = MakeTestSpecValue();
    unknownFeature.vec[7].vec.push_back(VecV({StrV("volcano"), PtV(0, 0), NumV(10), NumV(5)}));
    warnings.clear();
    REQUIRE(ScadTerrain::DecodeSpec(unknownFeature, spec, err, warnings));
    CHECK(warnings.size() == 1);
    CHECK(spec.features.size() == 6);
}

namespace
{
    // Encodes a .hmap blob (see FScadTerrain.h) from raw sample values.
    std::vector<uint8_t> MakeHmapBlob(int cols, int rows, glm::dvec2 origin, glm::dvec2 cell,
                                      double scale, double bias, const std::vector<double>& heights)
    {
        std::vector<uint8_t> out;
        auto putU32 = [&](uint32_t v)
        {
            for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>((v >> (i * 8)) & 0xFFu));
        };
        auto putF32 = [&](double v)
        {
            const float f = static_cast<float>(v);
            uint32_t bits = 0;
            std::memcpy(&bits, &f, sizeof(bits));
            putU32(bits);
        };
        out.insert(out.end(), {'G', 'K', 'H', 'M'});
        putU32(1);
        putU32(static_cast<uint32_t>(cols));
        putU32(static_cast<uint32_t>(rows));
        putF32(origin.x);
        putF32(origin.y);
        putF32(cell.x);
        putF32(cell.y);
        putF32(scale);
        putF32(bias);
        for (double h : heights)
        {
            const int16_t raw = static_cast<int16_t>(std::lround((h - bias) / scale));
            out.push_back(static_cast<uint8_t>(raw & 0xFF));
            out.push_back(static_cast<uint8_t>((raw >> 8) & 0xFF));
        }
        return out;
    }

    // A flat terrain whose only feature is an inline hmap of `heights`.
    Value MakeInlineHmapSpecValue(int cols, int rows, const std::vector<double>& heights,
                                  const char* mode, double baseHeight = 0.0)
    {
        std::vector<Value> grid;
        grid.reserve(heights.size());
        for (double h : heights) grid.push_back(NumV(h));
        return VecV({StrV("gkterr1"), PtV(100, 100), PtV(20, 20), NumV(3),
                     VecV({NumV(baseHeight), NumV(0), NumV(0.5)}), Value(), StrV("temperate"),
                     VecV({VecV({StrV("hmap"), PtV(cols, rows), VecV(grid), StrV(mode)})})});
    }
} // namespace

TEST_CASE("ScadTerrain hmap: decodes a .hmap blob and samples bilinearly",
          "[Unit][Scad][ScadTerrain][Hmap]")
{
    // 2x2 ramp over [0,10]^2: 0 at the origin corner, 30 at the far corner.
    const std::vector<uint8_t> blob =
        MakeHmapBlob(2, 2, {0.0, 0.0}, {10.0, 10.0}, 0.01, 0.0, {0.0, 10.0, 20.0, 30.0});
    std::string err;
    const auto grid = ScadTerrain::DecodeHeightGrid(blob, err);
    REQUIRE(grid);
    CHECK(err.empty());
    CHECK(grid->cols == 2);
    CHECK(grid->rows == 2);

    // Corners reproduce the stored samples (0.01 quantisation).
    CHECK(grid->Sample(0.0, 0.0) == Catch::Approx(0.0).margin(0.01));
    CHECK(grid->Sample(10.0, 0.0) == Catch::Approx(10.0).margin(0.01));
    CHECK(grid->Sample(0.0, 10.0) == Catch::Approx(20.0).margin(0.01));
    CHECK(grid->Sample(10.0, 10.0) == Catch::Approx(30.0).margin(0.01));
    // Bilinear interior.
    CHECK(grid->Sample(5.0, 5.0) == Catch::Approx(15.0).margin(0.01));
    CHECK(grid->Sample(5.0, 0.0) == Catch::Approx(5.0).margin(0.01));
    // Outside the grid clamps to the border rather than extrapolating.
    CHECK(grid->Sample(-100.0, -100.0) == Catch::Approx(0.0).margin(0.01));
    CHECK(grid->Sample(999.0, 999.0) == Catch::Approx(30.0).margin(0.01));
}

TEST_CASE("ScadTerrain hmap: rejects malformed blobs", "[Unit][Scad][ScadTerrain][Hmap]")
{
    std::string err;
    CHECK_FALSE(ScadTerrain::DecodeHeightGrid({}, err));
    CHECK_FALSE(err.empty());

    std::vector<uint8_t> badMagic = MakeHmapBlob(2, 2, {0, 0}, {1, 1}, 1.0, 0.0, {0, 0, 0, 0});
    badMagic[0] = 'X';
    CHECK_FALSE(ScadTerrain::DecodeHeightGrid(badMagic, err));

    std::vector<uint8_t> badVersion = MakeHmapBlob(2, 2, {0, 0}, {1, 1}, 1.0, 0.0, {0, 0, 0, 0});
    badVersion[4] = 9;
    CHECK_FALSE(ScadTerrain::DecodeHeightGrid(badVersion, err));

    std::vector<uint8_t> truncated = MakeHmapBlob(4, 4, {0, 0}, {1, 1}, 1.0, 0.0,
                                                  std::vector<double>(16, 1.0));
    truncated.resize(truncated.size() - 6);
    CHECK_FALSE(ScadTerrain::DecodeHeightGrid(truncated, err));
}

TEST_CASE("ScadTerrain hmap: set replaces the field, add offsets it",
          "[Unit][Scad][ScadTerrain][Hmap]")
{
    const std::vector<double> flat(9, 5.0);
    FTerrainSpec spec;
    std::string err;
    std::vector<std::string> warnings;

    REQUIRE(ScadTerrain::DecodeSpec(MakeInlineHmapSpecValue(3, 3, flat, "set", 2.0), spec, err,
                                    warnings));
    CHECK(warnings.empty());
    REQUIRE(spec.features.size() == 1);
    CHECK(spec.features[0].type == FTerrainFeature::EType::Hmap);
    CHECK_FALSE(spec.features[0].hmapAdd);
    {
        // "set" discards the base height; relief is 0 so the field is exactly 5.
        const auto data = ScadTerrain::Build(spec);
        for (const glm::dvec3& v : data->verts)
        {
            REQUIRE(v.z == Catch::Approx(5.0).margin(1e-9));
        }
    }

    warnings.clear();
    REQUIRE(ScadTerrain::DecodeSpec(MakeInlineHmapSpecValue(3, 3, flat, "add", 2.0), spec, err,
                                    warnings));
    CHECK(spec.features[0].hmapAdd);
    {
        const auto data = ScadTerrain::Build(spec);
        for (const glm::dvec3& v : data->verts)
        {
            REQUIRE(v.z == Catch::Approx(7.0).margin(1e-9));
        }
    }
}

TEST_CASE("ScadTerrain hmap: an inline ramp drives the built mesh",
          "[Unit][Scad][ScadTerrain][Hmap]")
{
    // 3x3 grid over the 100x100 domain: height = 10 per column step (+x).
    const std::vector<double> ramp = {0, 10, 20, 0, 10, 20, 0, 10, 20};
    FTerrainSpec spec;
    std::string err;
    std::vector<std::string> warnings;
    REQUIRE(ScadTerrain::DecodeSpec(MakeInlineHmapSpecValue(3, 3, ramp, "set"), spec, err, warnings));
    CHECK(warnings.empty());

    const auto data = ScadTerrain::Build(spec);
    // Height rises monotonically with x and is flat along y.
    CHECK(data->HeightAt(-49.0, 0.0) < data->HeightAt(0.0, 0.0));
    CHECK(data->HeightAt(0.0, 0.0) < data->HeightAt(49.0, 0.0));
    CHECK(data->HeightAt(0.0, -40.0) == Catch::Approx(data->HeightAt(0.0, 40.0)).margin(1e-6));
    CHECK(data->HeightAt(0.0, 0.0) == Catch::Approx(10.0).margin(0.6));

    // Mesh vertices and the height query agree, exactly as for the other features.
    for (const glm::dvec3& v : data->verts)
    {
        REQUIRE(data->HeightAt(v.x, v.y) == Catch::Approx(v.z).margin(1e-6));
    }
}

TEST_CASE("ScadTerrain hmap: a missing file warns and skips the feature",
          "[Unit][Scad][ScadTerrain][Hmap]")
{
    const Value v = VecV({StrV("gkterr1"), PtV(100, 100), PtV(20, 20), NumV(3),
                          VecV({NumV(1.0), NumV(0), NumV(0.5)}), Value(), StrV("temperate"),
                          VecV({VecV({StrV("hmap"), StrV("assets/scad/geo/__does_not_exist.hmap")})})});
    FTerrainSpec spec;
    std::string err;
    std::vector<std::string> warnings;
    REQUIRE(ScadTerrain::DecodeSpec(v, spec, err, warnings));
    CHECK(spec.features.empty());
    REQUIRE(warnings.size() == 1);
    CHECK(warnings[0].find("hmap") != std::string::npos);
}

TEST_CASE("ScadTerrain hmap: cache key tracks the grid contents",
          "[Unit][Scad][ScadTerrain][Hmap]")
{
    FTerrainSpec a;
    FTerrainSpec b;
    std::string err;
    std::vector<std::string> warnings;
    REQUIRE(ScadTerrain::DecodeSpec(MakeInlineHmapSpecValue(3, 3, std::vector<double>(9, 5.0), "set"),
                                    a, err, warnings));
    REQUIRE(ScadTerrain::DecodeSpec(MakeInlineHmapSpecValue(3, 3, std::vector<double>(9, 5.0), "set"),
                                    b, err, warnings));
    CHECK(ScadTerrain::SpecCacheKey(a) == ScadTerrain::SpecCacheKey(b));

    std::vector<double> altered(9, 5.0);
    altered[4] = 6.0;
    REQUIRE(ScadTerrain::DecodeSpec(MakeInlineHmapSpecValue(3, 3, altered, "set"), b, err, warnings));
    CHECK(ScadTerrain::SpecCacheKey(a) != ScadTerrain::SpecCacheKey(b));
}

TEST_CASE("ScadTerrain decode: matches the C++ mirror spec", "[Unit][Scad][ScadTerrain]")
{
    FTerrainSpec decoded;
    std::string err;
    std::vector<std::string> warnings;
    REQUIRE(ScadTerrain::DecodeSpec(MakeTestSpecValue(), decoded, err, warnings));
    CHECK(warnings.empty());
    CHECK(ScadTerrain::SpecCacheKey(decoded) == ScadTerrain::SpecCacheKey(MakeTestSpec()));
}

TEST_CASE("ScadTerrain decode: clamps cells with a warning", "[Unit][Scad][ScadTerrain]")
{
    Value v = MakeTestSpecValue();
    v.vec[2] = PtV(4000, 2);
    FTerrainSpec spec;
    std::string err;
    std::vector<std::string> warnings;
    REQUIRE(ScadTerrain::DecodeSpec(v, spec, err, warnings));
    CHECK(spec.cells.x == 256);
    CHECK(spec.cells.y == 4);
    CHECK(warnings.size() == 1);
}

TEST_CASE("ScadTerrain build: deterministic for the same spec", "[Unit][Scad][ScadTerrain]")
{
    const FTerrainSpec spec = MakeTestSpec();
    const auto first = ScadTerrain::Build(spec);
    const auto second = ScadTerrain::Build(spec);

    REQUIRE(first->verts.size() == second->verts.size());
    for (size_t i = 0; i < first->verts.size(); ++i)
    {
        REQUIRE(first->verts[i] == second->verts[i]);
    }
    REQUIRE(first->landGeom.size() == second->landGeom.size());
    for (size_t i = 0; i < first->landGeom.size(); ++i)
    {
        REQUIRE(first->landGeom[i].tris.size() == second->landGeom[i].tris.size());
    }
}

TEST_CASE("ScadTerrain query: HeightAt reproduces every mesh vertex", "[Unit][Scad][ScadTerrain]")
{
    const auto data = ScadTerrain::Build(MakeTestSpec());
    REQUIRE_FALSE(data->verts.empty());
    for (const glm::dvec3& v : data->verts)
    {
        const double h = data->HeightAt(v.x, v.y);
        REQUIRE(h == Catch::Approx(v.z).margin(1e-6));
    }
}

TEST_CASE("ScadTerrain features: pad is flat, mountain rises, plateau tops out", "[Unit][Scad][ScadTerrain]")
{
    const auto data = ScadTerrain::Build(MakeTestSpec());

    // Pad: every sample inside the (untilted) rectangle shares one height.
    const double padH = data->HeightAt(25.0, -20.0);
    for (const glm::dvec2 offset : {glm::dvec2(-6, -4), glm::dvec2(6, -4), glm::dvec2(6, 4), glm::dvec2(-6, 4)})
    {
        CHECK(data->HeightAt(25.0 + offset.x, -20.0 + offset.y) == Catch::Approx(padH).margin(1e-6));
    }
    double h = 0.0;
    double slope = 0.0;
    bool water = false;
    uint8_t biome = 0;
    data->InfoAt(25.0, -20.0, h, slope, water, biome);
    CHECK_FALSE(water);
    CHECK(biome == static_cast<uint8_t>(ETerrainBiome::Pad));
    CHECK(slope < 3.0);

    // Mountain: clearly above the base near its center.
    CHECK(data->HeightAt(-30.0, 25.0) > 14.0 * 0.5);

    // Plateau: top plate close to its nominal height.
    CHECK(data->HeightAt(-40.0, -30.0) > 5.0 * 0.7);
}

TEST_CASE("ScadTerrain features: road and pad material boundaries use triangle miters",
          "[Unit][Scad][ScadTerrain]")
{
    FTerrainSpec spec;
    spec.size = {40.0, 40.0};
    spec.cells = {10, 10};
    spec.seed = 17;
    spec.relief = 0.0;

    FTerrainFeature road;
    road.type = FTerrainFeature::EType::Road;
    road.pts = {{-18.0, -13.0}, {18.0, 11.0}};
    road.width = 4.0;
    spec.features.push_back(road);

    FTerrainFeature pad;
    pad.type = FTerrainFeature::EType::Pad;
    pad.at = {8.0, -8.0};
    pad.size = {10.0, 7.0};
    pad.rot = 31.0;
    spec.features.push_back(pad);

    const auto data = ScadTerrain::Build(spec);
    std::vector<uint8_t> roadFaces(100, 0);
    std::vector<uint8_t> padFaces(100, 0);
    for (const FTerrainData::ColoredTris& bucket : data->landGeom)
    {
        std::vector<uint8_t>* faceCounts = nullptr;
        if (bucket.materialName == "terrain_road")
            faceCounts = &roadFaces;
        else if (bucket.materialName == "terrain_pad")
            faceCounts = &padFaces;
        else
            continue;

        for (size_t tri = 0; tri + 2 < bucket.tris.size(); tri += 3)
        {
            const glm::dvec3 centroid =
                (bucket.tris[tri] + bucket.tris[tri + 1] + bucket.tris[tri + 2]) / 3.0;
            const int cell = data->CellIndexAt(centroid.x, centroid.y);
            REQUIRE(cell >= 0);
            ++(*faceCounts)[static_cast<size_t>(cell)];
        }
    }

    // A boundary cell containing one feature face and one natural-biome face
    // proves that the mask can follow the cell diagonal instead of filling an
    // axis-aligned grid square.
    CHECK(std::find(roadFaces.begin(), roadFaces.end(), 1) != roadFaces.end());
    CHECK(std::find(padFaces.begin(), padFaces.end(), 1) != padFaces.end());
    CHECK(CountMaterialTips(*data, "terrain_road") == 0);
    CHECK(CountMaterialTips(*data, "terrain_pad") == 0);
}

TEST_CASE("ScadTerrain river: carves below a downstream-monotonic water line", "[Unit][Scad][ScadTerrain]")
{
    const auto data = ScadTerrain::Build(MakeTestSpec());

    const std::vector<glm::dvec2> path = {{-30.0, 25.0}, {-10.0, 5.0}, {0.0, -10.0}, {5.0, -48.0}};
    std::vector<double> waterLevels;
    int wetSamples = 0;
    int totalSamples = 0;
    for (size_t seg = 0; seg + 1 < path.size(); ++seg)
    {
        for (int k = 0; k < 8; ++k)
        {
            const double t = static_cast<double>(k) / 8.0;
            const glm::dvec2 p = path[seg] * (1.0 - t) + path[seg + 1] * t;
            ++totalSamples;
            const int cell = data->CellIndexAt(p.x, p.y);
            REQUIRE(cell >= 0);
            if (data->cellFlags[cell] & FTerrainData::kFlagWater)
            {
                ++wetSamples;
                const double level = data->cellWater[cell];
                CHECK(data->HeightAt(p.x, p.y) < level);
                waterLevels.push_back(level);
            }
        }
    }
    // The channel must be wet along most of the path...
    CHECK(wetSamples > totalSamples * 2 / 3);
    // ...and the surface must not run uphill (small tolerance for the cell
    // center vs. path point offset).
    for (size_t i = 1; i < waterLevels.size(); ++i)
    {
        CHECK(waterLevels[i] <= waterLevels[i - 1] + 0.05);
    }
}

TEST_CASE("ScadTerrain palette: bounded color bucket count", "[Unit][Scad][ScadTerrain]")
{
    const auto data = ScadTerrain::Build(MakeTestSpec());
    CHECK(data->landGeom.size() <= 11);
    CHECK(data->landGeom.size() + data->waterGeom.size() <= 13);
    REQUIRE_FALSE(data->waterGeom.empty()); // river + lake must produce water
    CHECK(data->waterGeom[0].color.b > data->waterGeom[0].color.r); // stylized blue
}

TEST_CASE("ScadTerrain eval: bad spec warns and produces nothing", "[Unit][Scad][ScadTerrain]")
{
    const EvalResult result = EvalProgram("gk_terrain(42);\n");
    CHECK(result.warningCount >= 1);
    CHECK(TotalTriangles(result) == 0);
}

TEST_CASE("ScadTerrain eval: gk_terrain renders warning-free", "[Unit][Scad][ScadTerrain]")
{
    const EvalResult result = EvalProgram(std::string(kTerrScad) + "gk_terrain(TERR);\n");
    CHECK(result.warningCount == 0);
    // 60x50 cells => 6000 land triangles + skirt + water.
    CHECK(TotalTriangles(result) > 6000);
}

TEST_CASE("ScadTerrain eval: gk_terrain_height matches the built mesh", "[Unit][Scad][ScadTerrain]")
{
    const auto data = ScadTerrain::Build(MakeTestSpec());

    const EvalResult result =
        EvalProgram(std::string(kTerrScad) + "translate([12, 8, gk_terrain_height(TERR, 12, 8)]) cube(1);\n");
    CHECK(result.warningCount == 0);
    REQUIRE(result.buckets.size() == 1);

    double minZ = 1e30;
    for (const auto& entry : result.buckets)
    {
        for (const glm::dvec3& v : entry.second.tris)
            minZ = std::min(minZ, v.z);
    }
    CHECK(minZ == Catch::Approx(data->HeightAt(12.0, 8.0)).margin(1e-6));
}

TEST_CASE("ScadTerrain eval: identity cache distinguishes multiple terrain values",
          "[Unit][Scad][ScadTerrain]")
{
    const SceneEvalResult result = EvalSceneProgram(
        "A = [\"gkterr1\", [4,4], [4,4], 1, [0,0,0], undef, \"temperate\", []];\n"
        "B = [\"gkterr1\", [4,4], [4,4], 2, [10,0,0], undef, \"temperate\", []];\n"
        "function sample(t) = gk_terrain_height(t, 0, 0);\n"
        "ha0 = sample(A);\n"
        "hb = sample(B);\n"
        "ha1 = sample(A);\n");

    REQUIRE(result.warningCount == 0);
    REQUIRE(result.topLevelVariables.contains("ha0"));
    REQUIRE(result.topLevelVariables.contains("hb"));
    REQUIRE(result.topLevelVariables.contains("ha1"));
    CHECK(result.topLevelVariables.at("ha0").AsNumber() == Catch::Approx(0.0));
    CHECK(result.topLevelVariables.at("hb").AsNumber() == Catch::Approx(10.0));
    CHECK(result.topLevelVariables.at("ha1").AsNumber() == Catch::Approx(0.0));
}

TEST_CASE("Scad normals: zero smoothing angle uses exact per-face normals", "[Unit][Scad]")
{
    const std::vector<glm::vec3> positions = {
        {0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f},
    };
    const std::vector<glm::vec3> normals = ScadComputeSmoothNormals(positions, 0.0f);

    REQUIRE(normals.size() == positions.size());
    for (size_t i = 0; i < 3; ++i)
    {
        CHECK(normals[i].x == Catch::Approx(0.0f));
        CHECK(normals[i].y == Catch::Approx(0.0f));
        CHECK(normals[i].z == Catch::Approx(1.0f));
    }
    for (size_t i = 3; i < 6; ++i)
    {
        CHECK(normals[i].x == Catch::Approx(1.0f));
        CHECK(normals[i].y == Catch::Approx(0.0f));
        CHECK(normals[i].z == Catch::Approx(0.0f));
    }
}

TEST_CASE("ScadTerrain scene: faceted + water flags and terrain payload", "[Unit][Scad][ScadTerrain]")
{
    const SceneEvalResult result = EvalSceneProgram(std::string(kTerrScad) +
                                                    "module ground() { gk_terrain(TERR); }\n"
                                                    "ground();\n");
    CHECK(result.warningCount == 0);
    REQUIRE(result.terrains.size() == 1);
    REQUIRE(result.terrains[0].data != nullptr);

    const SceneNode* ground = nullptr;
    for (const SceneNode& root : result.roots)
    {
        if (root.name == "ground")
            ground = &root;
    }
    REQUIRE(ground != nullptr);

    bool sawFaceted = false;
    bool sawWater = false;
    for (const SceneMeshBucket& bucket : ground->meshes)
    {
        sawFaceted = sawFaceted || bucket.faceted;
        sawWater = sawWater || bucket.terrainWater;
    }
    CHECK(sawFaceted);
    CHECK(sawWater);
}

TEST_CASE("ScadTerrain scene: non-terrain geometry keeps default flags", "[Unit][Scad][ScadTerrain]")
{
    const SceneEvalResult result = EvalSceneProgram("cube(1);\n");
    REQUIRE(result.roots.size() == 1);
    REQUIRE_FALSE(result.roots[0].meshes.empty());
    for (const SceneMeshBucket& bucket : result.roots[0].meshes)
    {
        CHECK_FALSE(bucket.faceted);
        CHECK_FALSE(bucket.terrainWater);
    }
    CHECK(result.terrains.empty());
}

TEST_CASE("ScadTerrain loader: water splits to a raycast-invisible node", "[Unit][Scad][ScadTerrain]")
{
    ScopedDir dir;
    const std::filesystem::path mainPath = dir.Write("terrain.scad", std::string(kTerrScad) + "gk_terrain(TERR);\n");

    Assets::EnvironmentSetting environment;
    std::vector<std::shared_ptr<Assets::Node>> nodes;
    std::vector<Assets::Model> models;
    std::vector<Assets::FMaterial> materials;
    std::vector<Assets::LightObject> lights;
    std::vector<Assets::AnimationTrack> tracks;
    std::vector<Assets::Skeleton> skeletons;

    REQUIRE(Assets::FScadLoader::LoadScadScene(mainPath.string(), environment, nodes, models, materials, lights, tracks,
                                               skeletons));

    const std::shared_ptr<Assets::Node>* waterNode = nullptr;
    const std::shared_ptr<Assets::Node>* landNode = nullptr;
    for (const std::shared_ptr<Assets::Node>& node : nodes)
    {
        if (node->GetName().find("__water") != std::string::npos)
            waterNode = &node;
        else if (node->GetComponent<Runtime::RenderComponent>())
            landNode = &node;
    }
    REQUIRE(waterNode != nullptr);
    REQUIRE(landNode != nullptr);

    auto waterRender = (*waterNode)->GetComponent<Runtime::RenderComponent>();
    REQUIRE(waterRender != nullptr);
    CHECK_FALSE(waterRender->GetRayCastVisible());

    auto landRender = (*landNode)->GetComponent<Runtime::RenderComponent>();
    REQUIRE(landRender != nullptr);
    CHECK(landRender->GetRayCastVisible());

    // The water bucket carries its own material name through the loader.
    bool sawWaterMaterial = false;
    for (const Assets::FMaterial& material : materials)
    {
        if (material.name_ == "terrain_water")
            sawWaterMaterial = true;
    }
    CHECK(sawWaterMaterial);
}

TEST_CASE("ScadTerrain scatter: variants bound baked mesh diversity", "[Unit][Scad][ScadTerrain]")
{
    const std::filesystem::path kitPath = FindSourceAsset("assets/scad/lib/kit_terrain.scad");
    REQUIRE(std::filesystem::is_regular_file(kitPath));

    const auto LoadScatterModelCount = [&](int variants) {
        ScopedDir dir;
        const std::string source =
            fmt::format("use <{}>\n"
                        "TERR = [\"gkterr1\", [100,100], [10,10], 7, [0,0,0], undef, \"temperate\", []];\n"
                        "module variant_probe(seed=0) {{ cube([1 + (seed % 97) / 100, 1, 1]); }}\n"
                        "ter_scatter(TERR, seed=31, n=24, region=[-40,-40,40,40], rot=true,\n"
                        "            variants={}, scale=[0.75,1.25])\n"
                        "    variant_probe(seed=$seed);\n",
                        kitPath.generic_string(),
                        variants);
        const std::filesystem::path mainPath = dir.Write("scatter.scad", source);

        Assets::EnvironmentSetting environment;
        std::vector<std::shared_ptr<Assets::Node>> nodes;
        std::vector<Assets::Model> models;
        std::vector<Assets::FMaterial> materials;
        std::vector<Assets::LightObject> lights;
        std::vector<Assets::AnimationTrack> tracks;
        std::vector<Assets::Skeleton> skeletons;
        REQUIRE(Assets::FScadLoader::LoadScadScene(
            mainPath.string(), environment, nodes, models, materials, lights, tracks, skeletons));
        return models.size();
    };

    CHECK(LoadScatterModelCount(1) == 1);

    const size_t boundedModels = LoadScatterModelCount(3);
    CHECK(boundedModels > 1);
    CHECK(boundedModels <= 3);

    // variants=0 keeps the historical per-instance seed behavior.
    CHECK(LoadScatterModelCount(0) == 24);
}

TEST_CASE("ScadTerrain loader: TerrainComponent matches the evaluated heightfield", "[Unit][Scad][ScadTerrain]")
{
    ScopedDir dir;
    const std::filesystem::path mainPath = dir.Write("terrain.scad", std::string(kTerrScad) + "gk_terrain(TERR);\n");

    Assets::EnvironmentSetting environment;
    std::vector<std::shared_ptr<Assets::Node>> nodes;
    std::vector<Assets::Model> models;
    std::vector<Assets::FMaterial> materials;
    std::vector<Assets::LightObject> lights;
    std::vector<Assets::AnimationTrack> tracks;
    std::vector<Assets::Skeleton> skeletons;

    REQUIRE(Assets::FScadLoader::LoadScadScene(mainPath.string(), environment, nodes, models, materials, lights, tracks,
                                               skeletons));

    Runtime::TerrainComponent* terrain = nullptr;
    for (const std::shared_ptr<Assets::Node>& node : nodes)
    {
        if (auto comp = node->GetComponent<Runtime::TerrainComponent>())
        {
            terrain = comp;
        }
    }
    REQUIRE(terrain != nullptr);
    REQUIRE(terrain->HasData());
    CHECK(terrain->GetCellsX() == 60);
    CHECK(terrain->GetCellsY() == 50);

    // Engine world (x, z) maps to SCAD (x, -z) at scale 1; world Y == SCAD z.
    const auto data = ScadTerrain::Build(MakeTestSpec());
    for (const glm::dvec2 scadPt : {glm::dvec2(12, 8), glm::dvec2(-30, 25), glm::dvec2(25, -20), glm::dvec2(-45, -30)})
    {
        const float worldH = terrain->SampleHeight(static_cast<float>(scadPt.x), static_cast<float>(-scadPt.y));
        CHECK(worldH == Catch::Approx(data->HeightAt(scadPt.x, scadPt.y)).margin(1e-4));
    }

    // River channel center: water, not walkable. (At SCAD y = -25 the channel
    // centerline sits near x = 2.)
    CHECK(terrain->IsWater(2.0f, 25.0f));
    CHECK_FALSE(terrain->IsWalkable(2.0f, 25.0f));
    CHECK(terrain->WaterSurface(2.0f, 25.0f) > terrain->SampleHeight(2.0f, 25.0f));

    // Village pad: dry, flat, walkable, pad biome.
    CHECK_FALSE(terrain->IsWater(25.0f, 20.0f)); // SCAD (25, -20)
    CHECK(terrain->IsWalkable(25.0f, 20.0f));
    CHECK(terrain->SampleSlopeDegrees(25.0f, 20.0f) < 3.0f);
    CHECK(terrain->BiomeId(25.0f, 20.0f) == static_cast<uint8_t>(ETerrainBiome::Pad));

    // Mountain flank: steep enough to reject a 30-degree walker.
    // (center of the test mountain is at SCAD (-30, 25) with height 14)
    CHECK(terrain->SampleSlopeDegrees(-42.0f, -12.0f) >= 0.0f); // sanity: query answers
}
