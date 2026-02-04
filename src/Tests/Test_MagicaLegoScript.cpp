#include <catch2/catch_test_macros.hpp>
#include "Application/MagicaLego/MagicaLegoScriptParser.hpp"
#include "Application/MagicaLego/MagicaLegoCommands.hpp"

// ==================== Cursor Tests ====================
// Note: These tests use header-only FCursor implementation
// Command execution tests are omitted as they require full GameInstance

TEST_CASE("MagicaLego Cursor - Direction Helpers", "[Cursor]")
{
    using namespace MagicaLego;

    SECTION("GetDirectionVector returns correct vectors")
    {
        REQUIRE(GetDirectionVector(EOrientation::EO_North) == glm::i16vec3{0, 0, -1});
        REQUIRE(GetDirectionVector(EOrientation::EO_East) == glm::i16vec3{1, 0, 0});
        REQUIRE(GetDirectionVector(EOrientation::EO_South) == glm::i16vec3{0, 0, 1});
        REQUIRE(GetDirectionVector(EOrientation::EO_West) == glm::i16vec3{-1, 0, 0});
    }

    SECTION("TurnLeft rotates counter-clockwise")
    {
        REQUIRE(TurnLeft(EOrientation::EO_North) == EOrientation::EO_West);
        REQUIRE(TurnLeft(EOrientation::EO_West) == EOrientation::EO_South);
        REQUIRE(TurnLeft(EOrientation::EO_South) == EOrientation::EO_East);
        REQUIRE(TurnLeft(EOrientation::EO_East) == EOrientation::EO_North);
    }

    SECTION("TurnRight rotates clockwise")
    {
        REQUIRE(TurnRight(EOrientation::EO_North) == EOrientation::EO_East);
        REQUIRE(TurnRight(EOrientation::EO_East) == EOrientation::EO_South);
        REQUIRE(TurnRight(EOrientation::EO_South) == EOrientation::EO_West);
        REQUIRE(TurnRight(EOrientation::EO_West) == EOrientation::EO_North);
    }

    SECTION("TurnAround reverses direction")
    {
        REQUIRE(TurnAround(EOrientation::EO_North) == EOrientation::EO_South);
        REQUIRE(TurnAround(EOrientation::EO_East) == EOrientation::EO_West);
        REQUIRE(TurnAround(EOrientation::EO_South) == EOrientation::EO_North);
        REQUIRE(TurnAround(EOrientation::EO_West) == EOrientation::EO_East);
    }

    SECTION("Four left turns return to original")
    {
        EOrientation dir = EOrientation::EO_North;
        dir = TurnLeft(dir);
        dir = TurnLeft(dir);
        dir = TurnLeft(dir);
        dir = TurnLeft(dir);
        REQUIRE(dir == EOrientation::EO_North);
    }
}

TEST_CASE("MagicaLego Cursor - Movement", "[Cursor]")
{
    using namespace MagicaLego;

    SECTION("Initial state")
    {
        FCursor cursor;
        REQUIRE(cursor.position == glm::i16vec3{0, 0, 0});
        REQUIRE(cursor.facing == EOrientation::EO_North);
    }

    SECTION("Move forward when facing North")
    {
        FCursor cursor;
        cursor.MoveForward(3);
        REQUIRE(cursor.position == glm::i16vec3{0, 0, -3});
    }

    SECTION("Move forward when facing East")
    {
        FCursor cursor;
        cursor.facing = EOrientation::EO_East;
        cursor.MoveForward(2);
        REQUIRE(cursor.position == glm::i16vec3{2, 0, 0});
    }

    SECTION("Move backward")
    {
        FCursor cursor;
        cursor.MoveBackward(2);
        REQUIRE(cursor.position == glm::i16vec3{0, 0, 2});
    }

    SECTION("Move left when facing North")
    {
        FCursor cursor;
        cursor.MoveLeft(1);
        REQUIRE(cursor.position == glm::i16vec3{-1, 0, 0});
    }

    SECTION("Move right when facing North")
    {
        FCursor cursor;
        cursor.MoveRight(1);
        REQUIRE(cursor.position == glm::i16vec3{1, 0, 0});
    }

    SECTION("Move up and down")
    {
        FCursor cursor;
        cursor.MoveUp(3);
        REQUIRE(cursor.position == glm::i16vec3{0, 3, 0});
        cursor.MoveDown(1);
        REQUIRE(cursor.position == glm::i16vec3{0, 2, 0});
    }

    SECTION("Turn and move")
    {
        FCursor cursor;
        cursor.TurnRight();  // Now facing East
        cursor.MoveForward(2);
        REQUIRE(cursor.position == glm::i16vec3{2, 0, 0});
    }

    SECTION("Complex movement")
    {
        FCursor cursor;
        cursor.MoveForward(2);   // (0, 0, -2)
        cursor.TurnLeft();       // Facing West
        cursor.MoveForward(1);   // (-1, 0, -2)
        cursor.MoveUp(3);        // (-1, 3, -2)
        REQUIRE(cursor.position == glm::i16vec3{-1, 3, -2});
    }

    SECTION("TurnAround method")
    {
        FCursor cursor;
        cursor.TurnAround();
        REQUIRE(cursor.facing == EOrientation::EO_South);
    }
}

TEST_CASE("MagicaLego Cursor - GetPositionOffset", "[Cursor]")
{
    using namespace MagicaLego;

    SECTION("Offset when facing North")
    {
        FCursor cursor;
        cursor.position = {5, 2, 3};
        cursor.facing = EOrientation::EO_North;

        // forward = -Z, right = +X
        auto pos = cursor.GetPositionOffset(2, 1, 0);
        REQUIRE(pos == glm::i16vec3{6, 2, 1});  // +1 right (X), +2 forward (-Z means z-2=1)
    }

    SECTION("Offset when facing East")
    {
        FCursor cursor;
        cursor.position = {0, 0, 0};
        cursor.facing = EOrientation::EO_East;

        // forward = +X, right = +Z
        auto pos = cursor.GetPositionOffset(3, 0, 1);
        REQUIRE(pos == glm::i16vec3{3, 1, 0});
    }
}

TEST_CASE("MagicaLego Cursor - FScanResult", "[Cursor]")
{
    using namespace MagicaLego;

    SECTION("ToJson produces valid JSON")
    {
        FScanResult result;
        result.cursorPos = {1, 2, 3};
        result.cursorFacing = EOrientation::EO_East;

        FScanEntry entry;
        entry.relativePos = {0, 1, 0};
        entry.blockType = "Block1x1";
        entry.colorIndex = 5;
        result.blocks.push_back(entry);

        std::string json = result.ToJson();
        REQUIRE(json.find("\"cursorPos\"") != std::string::npos);
        REQUIRE(json.find("\"cursorFacing\"") != std::string::npos);
        REQUIRE(json.find("\"blocks\"") != std::string::npos);
        REQUIRE(json.find("east") != std::string::npos);
    }
}

// ==================== Original Script Tests ====================

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
