#include <catch2/catch_all.hpp>

#include "Application/Editor/ScadStudio/ScadPromptContext.hpp"

using namespace ScadStudio;

TEST_CASE("BuildScadUserPrompt prefers the focused module scope", "[Unit][SCAD][Studio]")
{
    const std::string currentSource = "use <cup.scad>;\n\ncup();\n";
    FScadProjectFile mainFile;
    mainFile.path = "main.scad";
    mainFile.source = currentSource;
    FScadProjectFile moduleFile;
    moduleFile.path = "cup.scad";
    moduleFile.source = "module cup()\n{\n    cylinder(h = 10, r = 4);\n}\n";
    const std::vector<FScadProjectFile> files{mainFile, moduleFile};

    FScadEditScope scope;
    scope.activeFilePath = "main.scad";
    scope.focusedModuleName = "cup";
    scope.focusedModuleFilePath = "cup.scad";

    const std::string prompt = BuildScadUserPrompt(currentSource, files, scope, "make it taller");

    REQUIRE(prompt.find("Current focused module preview:") != std::string::npos);
    REQUIRE(prompt.find("- Module: cup") != std::string::npos);
    REQUIRE(prompt.find("- File: cup.scad") != std::string::npos);
    REQUIRE(prompt.find("DEFAULT edit target") != std::string::npos);
    REQUIRE(prompt.find("Keep unrelated files and modules unchanged") != std::string::npos);
    REQUIRE(prompt.find("COMPLETE contents of cup.scad") != std::string::npos);
    REQUIRE(prompt.find("User request:\nmake it taller") != std::string::npos);
}

TEST_CASE("BuildScadUserPrompt falls back to the selected file when no module is focused", "[Unit][SCAD][Studio]")
{
    const std::string currentSource = "use <cup.scad>;\n\ncup();\n";
    FScadProjectFile mainFile;
    mainFile.path = "main.scad";
    mainFile.source = currentSource;
    FScadProjectFile moduleFile;
    moduleFile.path = "cup.scad";
    moduleFile.source = "module cup()\n{\n    cylinder(h = 10, r = 4);\n}\n";
    const std::vector<FScadProjectFile> files{mainFile, moduleFile};

    FScadEditScope scope;
    scope.activeFilePath = "main.scad";

    const std::string prompt = BuildScadUserPrompt(currentSource, files, scope, "add a base");

    REQUIRE(prompt.find("Preferred file to edit: main.scad") != std::string::npos);
    REQUIRE(prompt.find("Current focused module preview:") == std::string::npos);
}
