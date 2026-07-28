#include <catch2/catch_test_macros.hpp>

#include "AI/ScadStudioSessionImporter.hpp"

#include <fstream>
#include <nlohmann/json.hpp>

using namespace ScadLibrary::AI;

TEST_CASE("ScadStudio importer selects active file without overwriting assets",
          "[AI][ScadLibrary][Migration]")
{
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "gknext_scadstudio_import_test";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    {
        std::ofstream output(root / "sessions.json");
        output << R"({"sessions":[{"id":"session-1","title":"Old scene","updatedAt":42}]})";
    }
    {
        nlohmann::json session = {
            {"title", "Old scene"},
            {"updatedAt", 42},
            {"currentSource", "cube(1);"},
            {"activeFilePath", "parts/active.scad"},
            {"files", {
                {{"path", "main.scad"}, {"source", "cube(2);"}},
                {{"path", "parts/active.scad"}, {"source", "sphere(3);"}},
                {{"path", "../escape.scad"}, {"source", "cube(99);"}},
            }},
        };
        std::ofstream output(root / "session-1.json");
        output << session.dump(2);
    }
    std::vector<std::string> warnings;
    const auto candidates = FScadStudioSessionImporter::Scan(root, warnings);
    REQUIRE(candidates.size() == 1);
    REQUIRE(candidates[0].source == "sphere(3);");
    REQUIRE(candidates[0].activeFilePath == "parts/active.scad");
    REQUIRE(candidates[0].fileCount == 3);
    REQUIRE_FALSE(candidates[0].warnings.empty());
    REQUIRE(std::filesystem::exists(root / "session-1.json"));
    std::filesystem::remove_all(root, error);
}
