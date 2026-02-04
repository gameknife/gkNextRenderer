#include <catch2/catch_test_macros.hpp>
#include "Application/MagicaLego/MagicaLegoScriptParser.hpp"

TEST_CASE("MagicaLego Script Parser - ValidateAndFix", "[Script]")
{
    SECTION("Valid script passes validation")
    {
        std::string script = R"(
repeat 3 as i
    place Block1x1/#0 $i 0 0
end
)";
        auto result = MagicaLego::FScriptParser::ValidateAndFix(script);
        REQUIRE(result.valid == true);
        REQUIRE(result.warnings.empty());
    }

    SECTION("Missing end is auto-fixed")
    {
        std::string script = R"(
repeat 3 as i
    place Block1x1/#0 $i 0 0
)";
        auto result = MagicaLego::FScriptParser::ValidateAndFix(script);
        REQUIRE(result.valid == false);
        REQUIRE(result.warnings.size() == 1);
        REQUIRE(result.fixedScript.find("end") != std::string::npos);
    }

    SECTION("Multiple missing ends are auto-fixed")
    {
        std::string script = R"(
repeat 3 as x
    repeat 3 as z
        place Block1x1/#0 $x 0 $z
)";
        auto result = MagicaLego::FScriptParser::ValidateAndFix(script);
        REQUIRE(result.valid == false);
        REQUIRE(result.warnings.size() == 1);

        // Parse the fixed script - should work
        MagicaLego::FScriptParser parser;
        std::string error;
        auto commands = parser.Parse(result.fixedScript, error);
        REQUIRE(error.empty());
        REQUIRE(commands.size() == 9);
    }
}

TEST_CASE("MagicaLego Script Parser - Variable Substitution", "[Script]")
{
    MagicaLego::FScriptContext context;
    context.SetVariable("x", 5);
    context.SetVariable("y", 10);

    SECTION("Simple variable substitution")
    {
        REQUIRE(context.SubstituteVariables("$x") == "5");
        REQUIRE(context.SubstituteVariables("$y") == "10");
        REQUIRE(context.SubstituteVariables("place Block1x1/#0 $x $y 0") == "place Block1x1/#0 5 10 0");
    }

    SECTION("Expression substitution with subtraction")
    {
        REQUIRE(context.SubstituteVariables("$(x - 2)") == "3");
        REQUIRE(context.SubstituteVariables("$(y - 5)") == "5");
    }

    SECTION("Expression substitution with addition")
    {
        REQUIRE(context.SubstituteVariables("$(x + 2)") == "7");
        REQUIRE(context.SubstituteVariables("$(y + 5)") == "15");
    }

    SECTION("Expression in place command")
    {
        context.SetVariable("x_offset", 3);
        std::string result = context.SubstituteVariables("place Plate1x1/#1 $(x_offset - 2) 0 0");
        REQUIRE(result == "place Plate1x1/#1 1 0 0");
    }
}

TEST_CASE("MagicaLego Script Parser - Full Script Parsing", "[Script]")
{
    MagicaLego::FScriptParser parser;
    std::string error;

    SECTION("Simple repeat loop")
    {
        std::string script = R"(
repeat 3 as i
    place Block1x1/#0 $i 0 0
end
)";
        auto commands = parser.Parse(script, error);
        REQUIRE(error.empty());
        REQUIRE(commands.size() == 3);
        REQUIRE(commands[0] == "place Block1x1/#0 0 0 0");
        REQUIRE(commands[1] == "place Block1x1/#0 1 0 0");
        REQUIRE(commands[2] == "place Block1x1/#0 2 0 0");
    }

    SECTION("Repeat with expression")
    {
        std::string script = R"(
repeat 3 as x
    place Block1x1/#0 $(x - 1) 0 0
end
)";
        auto commands = parser.Parse(script, error);
        REQUIRE(error.empty());
        REQUIRE(commands.size() == 3);
        REQUIRE(commands[0] == "place Block1x1/#0 -1 0 0");
        REQUIRE(commands[1] == "place Block1x1/#0 0 0 0");
        REQUIRE(commands[2] == "place Block1x1/#0 1 0 0");
    }

    SECTION("Inline comments")
    {
        std::string script = R"(
place Block1x1/#0 0 0 0 # This is a comment
place Block1x1/#1 1 0 0
)";
        auto commands = parser.Parse(script, error);
        REQUIRE(error.empty());
        REQUIRE(commands.size() == 2);
        REQUIRE(commands[0] == "place Block1x1/#0 0 0 0");
        REQUIRE(commands[1] == "place Block1x1/#1 1 0 0");
    }

    SECTION("Gemini style script")
    {
        std::string script = R"(
# Floor (3x3)
repeat 3 as x_offset
    repeat 3 as z_offset
        place Plate1x1/#1 $(x_offset - 1) 0 $(z_offset - 1)
    end
end
)";
        auto commands = parser.Parse(script, error);
        REQUIRE(error.empty());
        REQUIRE(commands.size() == 9);
        // First iteration: x_offset=0, z_offset=0,1,2
        REQUIRE(commands[0] == "place Plate1x1/#1 -1 0 -1");
        REQUIRE(commands[1] == "place Plate1x1/#1 -1 0 0");
        REQUIRE(commands[2] == "place Plate1x1/#1 -1 0 1");
    }
}
