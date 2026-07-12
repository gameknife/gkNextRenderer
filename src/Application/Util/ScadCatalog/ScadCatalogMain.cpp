// ScadCatalog: headless generator for the machine-readable SCAD kit catalog
// (assets/scad/lib/catalog.json, design docs/designs/scad-scene-compose-design.md §4.2).
//
// For every kit_*.scad under the lib directory it lists the module signatures
// (shared scanner with ScadLibrary's KitCatalog) and evaluates each module once
// with default arguments through the real SCAD evaluator (CPU only, no GPU) to
// record footprint / height / triangle count. Composers and LLM prompts read
// the catalog instead of re-parsing the kits.
//
// Typical invocation (paths default to the assets next to the executable):
//   ScadCatalog [--lib <abs assets/scad/lib>] [--out <catalog.json>] [--fn 12]

#include <iostream>
#include <cxxopts.hpp>
#include <nlohmann/json.hpp>

#include "Application/Editor/ScadLibrary/KitCatalog.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/GameInstance.hpp"
#include "Engine/Utilities/FileHelper.hpp"
#include "Modules/ScadLoader/FScadEvaluator.h"
#include "Modules/ScadLoader/FScadLexer.h"
#include "Modules/ScadLoader/FScadParser.h"
#include "Modules/ScadLoader/FScadShared.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

std::unique_ptr<NextGameInstanceBase> CreateGameInstance(Vulkan::WindowConfig& config, Runtime::Config::Options& options,
                                                         NextEngine* engine)
{
    return std::make_unique<NextGameInstanceVoid>(config, options, engine);
}

namespace
{
    // Kit-level scale class (design §4.1 point 5): warns future composers when
    // human-scale interior parts get mixed into city-scale layouts.
    std::string ScaleClassFor(const std::string& kitName)
    {
        if (kitName == "kit_office" || kitName == "kit_airport")
        {
            return "human";
        }
        if (kitName == "kit_old_city")
        {
            return "mid";
        }
        if (kitName.rfind("kit_city", 0) == 0)
        {
            return "city";
        }
        return "mid";
    }

    std::string InferPrefix(const ScadLibrary::FKitInfo& kit)
    {
        if (kit.modules.empty())
        {
            return "";
        }
        const std::string& name = kit.modules.front().name;
        const size_t underscore = name.find('_');
        return underscore == std::string::npos ? "" : name.substr(0, underscore + 1);
    }

    // Split a normalized parameter list at top-level commas into name/default pairs.
    nlohmann::ordered_json ParamListJson(const std::string& params)
    {
        nlohmann::ordered_json list = nlohmann::ordered_json::array();
        int depth = 0;
        std::string current;
        auto flush = [&]()
        {
            if (current.empty())
            {
                return;
            }
            const size_t eq = current.find('=');
            nlohmann::ordered_json entry;
            auto trim = [](std::string s)
            {
                const size_t b = s.find_first_not_of(' ');
                if (b == std::string::npos)
                {
                    return std::string();
                }
                const size_t e = s.find_last_not_of(' ');
                return s.substr(b, e - b + 1);
            };
            if (eq == std::string::npos)
            {
                entry["name"] = trim(current);
            }
            else
            {
                entry["name"] = trim(current.substr(0, eq));
                entry["default"] = trim(current.substr(eq + 1));
            }
            list.push_back(entry);
            current.clear();
        };
        for (const char c : params)
        {
            if (c == '(' || c == '[')
            {
                depth++;
            }
            else if (c == ')' || c == ']')
            {
                depth--;
            }
            if (c == ',' && depth == 0)
            {
                flush();
                continue;
            }
            current.push_back(c);
        }
        flush();
        return list;
    }
} // namespace

int main(int argc, const char* argv[]) noexcept
{
    try
    {
        std::string libDir;
        std::string outPath;
        int fnSegments = 12;

        cxxopts::Options options("ScadCatalog", "Generate catalog.json for the SCAD kit libraries");
        options.add_options()
            ("lib", "kit library directory", cxxopts::value<std::string>(libDir)->default_value(""))
            ("out", "output catalog.json path", cxxopts::value<std::string>(outPath)->default_value(""))
            ("fn", "$fn used while evaluating modules", cxxopts::value<int>(fnSegments)->default_value("12"))
            ("h,help", "Print usage");
        const auto parsed = options.parse(argc, argv);
        if (parsed.count("help") != 0)
        {
            std::cout << options.help() << std::endl;
            return 0;
        }
        if (libDir.empty())
        {
            libDir = Utilities::FileHelper::GetPlatformFilePath("assets/scad/lib");
        }
        if (outPath.empty())
        {
            outPath = (std::filesystem::path(libDir) / "catalog.json").string();
        }

        const std::vector<ScadLibrary::FKitInfo> kits = ScadLibrary::ScanKits(libDir);
        if (kits.empty())
        {
            std::cerr << "no kit_*.scad found under " << libDir << std::endl;
            return 1;
        }

        nlohmann::ordered_json catalog;
        catalog["version"] = 1;
        catalog["fn"] = fnSegments;
        catalog["kits"] = nlohmann::ordered_json::array();

        int totalModules = 0;
        int totalFailed = 0;
        for (const ScadLibrary::FKitInfo& kit : kits)
        {
            Assets::Scad::ScadProgram program;
            std::string error;
            if (!Assets::Scad::LoadScadProgram(kit.filePath, program, error))
            {
                std::cerr << kit.name << ": failed to load program: " << error << std::endl;
                return 1;
            }

            nlohmann::ordered_json kitJson;
            kitJson["name"] = kit.name;
            kitJson["file"] = std::filesystem::path(kit.filePath).filename().string();
            kitJson["prefix"] = InferPrefix(kit);
            kitJson["scaleClass"] = ScaleClassFor(kit.name);
            kitJson["modules"] = nlohmann::ordered_json::array();

            for (const ScadLibrary::FKitModuleInfo& moduleInfo : kit.modules)
            {
                nlohmann::ordered_json moduleJson;
                moduleJson["name"] = moduleInfo.name;
                moduleJson["category"] = moduleInfo.category;
                moduleJson["params"] = moduleInfo.params;
                moduleJson["paramList"] = ParamListJson(moduleInfo.params);
                moduleJson["line"] = moduleInfo.line;

                // Evaluate `module();` with default arguments against the kit's
                // resolved definition tables.
                const std::string source = "$fn = " + std::to_string(fnSegments) + ";\n" + moduleInfo.name + "();\n";
                std::vector<Assets::Scad::Token> tokens;
                Assets::Scad::Scope scope;
                Assets::Scad::EvalResult result;
                Assets::ScadLoadOptions loadOptions{};
                bool ok = Assets::Scad::ScadLexer::Tokenize(source, tokens, error) &&
                          Assets::Scad::ScadParser::Parse(tokens, scope, error) &&
                          Assets::Scad::ScadEvaluator::Evaluate(scope, program.modules, program.functions,
                                                                loadOptions, result, error);
                if (ok && result.triangleCount > 0)
                {
                    glm::dvec3 minB(1e30), maxB(-1e30);
                    for (const auto& bucketEntry : result.buckets)
                    {
                        for (const glm::dvec3& p : bucketEntry.second.tris)
                        {
                            minB = glm::min(minB, p);
                            maxB = glm::max(maxB, p);
                        }
                    }
                    auto round2 = [](double v) { return std::round(v * 100.0) / 100.0; };
                    moduleJson["footprint"] = {round2(maxB.x - minB.x), round2(maxB.y - minB.y)};
                    moduleJson["height"] = round2(maxB.z);
                    moduleJson["zMin"] = round2(minB.z);
                    moduleJson["center"] = {round2((minB.x + maxB.x) * 0.5), round2((minB.y + maxB.y) * 0.5)};
                    moduleJson["triangles"] = result.triangleCount;
                    moduleJson["colors"] = result.buckets.size();
                    moduleJson["warnings"] = result.warningCount;
                    moduleJson["ok"] = true;
                }
                else
                {
                    // Typically a module that needs mandatory params or children();
                    // still listed so browsers/composers know it exists.
                    moduleJson["ok"] = false;
                    if (!ok && !error.empty())
                    {
                        moduleJson["error"] = error;
                    }
                    totalFailed++;
                }
                kitJson["modules"].push_back(moduleJson);
                totalModules++;
            }
            catalog["kits"].push_back(kitJson);
            std::cout << kit.name << ": " << kit.modules.size() << " modules" << std::endl;
        }

        std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
        if (!out)
        {
            std::cerr << "cannot write " << outPath << std::endl;
            return 1;
        }
        out << catalog.dump(2) << "\n";
        std::cout << "catalog: " << outPath << " (" << totalModules << " modules, " << totalFailed
                  << " without default-arg geometry)" << std::endl;
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "ScadCatalog fatal: " << e.what() << std::endl;
        return 1;
    }
}
