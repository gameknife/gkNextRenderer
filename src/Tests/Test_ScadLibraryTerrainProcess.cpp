#include <catch2/catch_all.hpp>

#include "Application/Editor/ScadLibrary/TerrainProcessDocument.hpp"
#include "Engine/Utilities/FileHelper.hpp"
#include "Modules/ScadLoader/FScadEvaluator.h"
#include "Modules/ScadLoader/FScadLexer.h"
#include "Modules/ScadLoader/FScadParser.h"
#include "Modules/ScadLoader/FScadShared.h"

#include <chrono>
#include <filesystem>
#include <fmt/format.h>
#include <fstream>
#include <regex>
#include <string>
#include <unordered_map>

using namespace Assets;
using namespace Assets::Scad;
using namespace ScadLibrary;

namespace
{
    struct FParsedProcessSource
    {
        Scope topLevel;
        std::map<std::string, Value> variables;
    };

    std::string ReadTextFile(const std::filesystem::path& path)
    {
        std::ifstream input(path, std::ios::binary);
        REQUIRE(input.is_open());
        return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    }

    std::filesystem::path FindTerrainDemo()
    {
        std::filesystem::path cursor = std::filesystem::current_path();
        while (!cursor.empty())
        {
            const std::filesystem::path candidate = cursor / "assets/scad/terrain_layout_demo.scad";
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
        return Utilities::FileHelper::GetPlatformFilePath("assets/scad/terrain_layout_demo.scad");
    }

    FParsedProcessSource ParseAndEvaluate(std::string source)
    {
        // The parser normally receives source after LoadScadProgram has
        // resolved use/include. Keep line breaks so AST line numbers still
        // point into the original source used by the process document.
        source = std::regex_replace(source, std::regex(R"((?:use|include)\s*<[^>]+>)"), "");

        std::vector<Token> tokens;
        std::string error;
        REQUIRE(ScadLexer::Tokenize(source, tokens, error));
        Scope parsed;
        REQUIRE(ScadParser::Parse(tokens, parsed, error));

        std::unordered_map<std::string, StmtPtr> modules;
        std::unordered_map<std::string, StmtPtr> functions;
        FParsedProcessSource result;
        for (const StmtPtr& statement : parsed)
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
                result.topLevel.push_back(statement);
            }
        }

        ScadLoadOptions options;
        SceneEvalResult evaluated;
        REQUIRE(ScadEvaluator::EvaluateScene(result.topLevel, modules, functions, options, evaluated, error));
        result.variables = std::move(evaluated.topLevelVariables);
        return result;
    }

    std::string MakeDependenciesAbsolute(const std::string& source, const std::filesystem::path& sourceDir)
    {
        static const std::regex useRegex(R"(((?:use|include)\s*<)([^>]+)(>))");
        std::string result;
        std::string::const_iterator cursor = source.begin();
        std::smatch match;
        while (std::regex_search(cursor, source.end(), match, useRegex))
        {
            result.append(cursor, match[0].first);
            std::filesystem::path dependency(match[2].str());
            if (!dependency.is_absolute())
            {
                dependency = sourceDir / dependency;
            }
            result += match[1].str() + dependency.lexically_normal().generic_string() + match[3].str();
            cursor = match[0].second;
        }
        result.append(cursor, source.end());
        return result;
    }
} // namespace

TEST_CASE("ScadLibrary terrain process editor round-trips terrain_layout_demo",
          "[Unit][Scad][ScadLibrary][TerrainProcess]")
{
    const std::filesystem::path demoPath = FindTerrainDemo();
    const std::string source = ReadTextFile(demoPath);
    const FParsedProcessSource parsed = ParseAndEvaluate(source);

    FTerrainProcessDocument document;
    std::string error;
    std::vector<std::string> warnings;
    REQUIRE(document.Parse(source, parsed.topLevel, parsed.variables, error, warnings));
    CHECK(warnings.empty());
    REQUIRE(document.Terrain().features.size() == 8);
    CHECK(document.Terrain().features[0].type == FTerrainFeature::EType::Mountain);
    CHECK(document.Terrain().features[7].type == FTerrainFeature::EType::Pad);
    CHECK(document.ActiveRuleCount() == 13);
    CHECK(std::count_if(document.Rules().begin(), document.Rules().end(), [](const FTerrainProcessRule& rule)
                        { return rule.type == ETerrainProcessRuleType::HeightAnchor; }) == 1);
    CHECK(std::count_if(document.Rules().begin(), document.Rules().end(), [](const FTerrainProcessRule& rule)
                        { return rule.type == ETerrainProcessRuleType::Place; }) == 8);
    CHECK(std::count_if(document.Rules().begin(), document.Rules().end(), [](const FTerrainProcessRule& rule)
                        { return rule.type == ETerrainProcessRuleType::Along; }) == 1);
    CHECK(std::count_if(document.Rules().begin(), document.Rules().end(), [](const FTerrainProcessRule& rule)
                        { return rule.type == ETerrainProcessRuleType::Scatter; }) == 3);
    auto scatter = std::find_if(document.Rules().begin(), document.Rules().end(), [](const FTerrainProcessRule& rule)
                                { return rule.type == ETerrainProcessRuleType::Scatter; });
    REQUIRE(scatter != document.Rules().end());
    CHECK(scatter->circularRegion);
    CHECK(scatter->regionRadius == Catch::Approx(115.0));

    document.Terrain().features[0].height = 31.0;
    document.Rules()[0].x = 51.5;
    scatter->circularRegion = true;
    scatter->regionCenter = {4.0, -6.0};
    scatter->regionRadius = 72.0;
    const auto along =
        std::find_if(document.Rules().begin(), document.Rules().end(),
                     [](const FTerrainProcessRule& rule) { return rule.type == ETerrainProcessRuleType::Along; });
    REQUIRE(along != document.Rules().end());
    document.RemoveRule(static_cast<size_t>(std::distance(document.Rules().begin(), along)));
    FTerrainProcessRule& added = document.AddRule(ETerrainProcessRuleType::Place, "oh_prop_signpost(seed = 99);");
    added.x = 12.0;
    added.y = -8.0;

    const std::string generated = document.BuildSource();
    CHECK(generated.find("// ---- 桥:横跨河道") != std::string::npos);
    CHECK(generated.find("gk_terrain_height(TERR, -8.5, -31)") != std::string::npos);
    CHECK(generated.find("oh_prop_signpost(seed = 99);") != std::string::npos);
    CHECK(generated.find("ter_along(") == std::string::npos);
    CHECK(generated.find("ter_scatter(TERR, 21, 90, [4, -6, 72]") != std::string::npos);

    const FParsedProcessSource reparsed = ParseAndEvaluate(generated);
    FTerrainProcessDocument roundTripped;
    REQUIRE(roundTripped.Parse(generated, reparsed.topLevel, reparsed.variables, error, warnings));
    REQUIRE(roundTripped.Terrain().features.size() == 8);
    CHECK(roundTripped.Terrain().features[0].height == Catch::Approx(31.0));
    CHECK(roundTripped.ActiveRuleCount() == 13);
    CHECK(roundTripped.Rules()[0].x == Catch::Approx(51.5));
    const auto roundTripScatter =
        std::find_if(roundTripped.Rules().begin(), roundTripped.Rules().end(),
                     [](const FTerrainProcessRule& rule) { return rule.type == ETerrainProcessRuleType::Scatter; });
    REQUIRE(roundTripScatter != roundTripped.Rules().end());
    CHECK(roundTripScatter->circularRegion);
    CHECK(roundTripScatter->regionRadius == Catch::Approx(72.0));

    const std::string loadableSource = MakeDependenciesAbsolute(generated, demoPath.parent_path());
    const std::filesystem::path temporaryPath = std::filesystem::temp_directory_path() /
        fmt::format("scadlibrary_terrain_process_{}.scad", std::chrono::steady_clock::now().time_since_epoch().count());
    {
        std::ofstream output(temporaryPath, std::ios::binary | std::ios::trunc);
        REQUIRE(output.is_open());
        output << loadableSource;
    }
    ScadProgram program;
    REQUIRE(LoadScadProgram(temporaryPath.string(), program, error));
    ScadLoadOptions options;
    SceneEvalResult evaluated;
    REQUIRE(ScadEvaluator::EvaluateScene(program.mainTopLevel, program.modules, program.functions, options, evaluated,
                                         error));
    CHECK(evaluated.warningCount == 0);
    CHECK(evaluated.terrains.size() == 1);
    CHECK(evaluated.triangleCount > 30000);
    std::error_code removeError;
    std::filesystem::remove(temporaryPath, removeError);
}
