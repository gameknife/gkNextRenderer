#include <catch2/catch_test_macros.hpp>

#include "Modules/ScadLoader/FScadSourceIndex.h"

using namespace Assets::Scad;

TEST_CASE("SCAD source index preserves exact module byte spans", "[AI][ScadLibrary][SourceIndex]")
{
    const std::string source =
        "use <kit_city.scad>\r\n"
        "// 中文 module fake() {}\r\n"
        "module first(a = \"}\") { cube(a); }\r\n"
        "function helper(x) = x * 2;\r\n"
        "module one_line(v = [1, 2]) sphere(v[0]);\r\n";
    FScadSourceIndex index;
    std::string error;
    REQUIRE(BuildScadSourceIndex(source, index, error));
    REQUIRE(index.definitions.size() == 3);
    const FScadDefinitionSpan* first = index.Find(EScadDefinitionKind::Module, "first");
    REQUIRE(first != nullptr);
    REQUIRE(source.substr(first->begin, first->end - first->begin) ==
            "module first(a = \"}\") { cube(a); }");
    const FScadDefinitionSpan* function = index.Find(EScadDefinitionKind::Function, "helper");
    REQUIRE(function != nullptr);
    REQUIRE(source.substr(function->begin, function->end - function->begin) ==
            "function helper(x) = x * 2;");
    const FScadDefinitionSpan* oneLine = index.Find(EScadDefinitionKind::Module, "one_line");
    REQUIRE(oneLine != nullptr);
    REQUIRE(source.substr(oneLine->begin, oneLine->end - oneLine->begin) ==
            "module one_line(v = [1, 2]) sphere(v[0]);");
}

TEST_CASE("SCAD source index rejects incomplete definitions", "[AI][ScadLibrary][SourceIndex]")
{
    FScadSourceIndex index;
    std::string error;
    REQUIRE_FALSE(BuildScadSourceIndex("module broken(x) { cube(x);", index, error));
    REQUIRE_FALSE(error.empty());
}
