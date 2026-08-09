#include "Engine/Common/CoreMinimal.hpp"
#include "Application/Editor/ScadStudio/ScadOutline.hpp"
#include "Application/Game/MagicaLego/MagicaLegoScriptParser.hpp"

#include <catch2/catch_all.hpp>

namespace
{
    struct FFixtureResult
    {
        bool success = false;
        bool repairAttempted = false;
        std::string error;
    };

    std::string ExtractClosedFence(const std::string& text, std::string_view language)
    {
        const std::string marker = "```" + std::string(language);
        const size_t fence = text.find(marker);
        if (fence == std::string::npos)
        {
            return {};
        }
        const size_t begin = text.find('\n', fence);
        if (begin == std::string::npos)
        {
            return {};
        }
        const size_t end = text.find("```", begin + 1);
        if (end == std::string::npos)
        {
            return {};
        }
        return text.substr(begin + 1, end - begin - 1);
    }

    template <typename Validator>
    FFixtureResult RunOneRepairFixture(const std::string& initial, const std::string& repaired, Validator validate)
    {
        FFixtureResult result = validate(initial);
        if (result.success)
        {
            return result;
        }
        result = validate(repaired);
        result.repairAttempted = true;
        if (!result.success)
        {
            result.error = "repair failed after one attempt: " + result.error;
        }
        return result;
    }

    FFixtureResult ValidateScadFixture(const std::string& response)
    {
        const std::string source = ExtractClosedFence(response, "scad");
        if (source.empty())
        {
            return {.error = "response contains no closed scad artifact"};
        }
        const auto outline = ScadStudio::BuildScadOutline(source);
        return {.success = outline.ok, .error = outline.error};
    }

    FFixtureResult ValidateMagicaFixture(const std::string& response)
    {
        const std::string script = ExtractClosedFence(response, "mlscript");
        if (script.empty())
        {
            return {.error = "response contains no closed mlscript artifact"};
        }
        const auto parsed = MagicaLego::FScriptParser::ValidateAndFix(script);
        return {.success = parsed.valid,
                .error = parsed.warnings.empty() ? std::string("script validation failed") : parsed.warnings.front()};
    }
}

TEST_CASE("ScadStudio generated artifact fixtures allow exactly one repair", "[Unit][AI][ScadStudio]")
{
    const std::string good = "```scad\ncube([1, 1, 1]);\n```";
    CHECK(RunOneRepairFixture(good, {}, ValidateScadFixture).success);

    const auto repairedFence = RunOneRepairFixture("```scad\ncube([1, 1, 1]);", good, ValidateScadFixture);
    CHECK(repairedFence.success);
    CHECK(repairedFence.repairAttempted);

    const auto failed = RunOneRepairFixture("```scad\ncube([);\n```", "```scad\nsphere(\n```", ValidateScadFixture);
    CHECK_FALSE(failed.success);
    CHECK(failed.repairAttempted);
    CHECK(failed.error.starts_with("repair failed after one attempt:"));
}

TEST_CASE("MagicaLego generated artifact fixtures allow exactly one repair", "[Unit][AI][MagicaLego]")
{
    const std::string good = "```mlscript\ngoto 0 0 0\nplace Block1x1/#1 here\n```";
    CHECK(RunOneRepairFixture(good, {}, ValidateMagicaFixture).success);

    const auto repairedFence = RunOneRepairFixture("```mlscript\nrepeat 2 as i\nmove up", good, ValidateMagicaFixture);
    CHECK(repairedFence.success);
    CHECK(repairedFence.repairAttempted);

    const auto failed = RunOneRepairFixture("```mlscript\nrepeat nope as i\n```",
                                            "```mlscript\nrepeat still_bad as i\n```", ValidateMagicaFixture);
    CHECK_FALSE(failed.success);
    CHECK(failed.repairAttempted);
    CHECK(failed.error.starts_with("repair failed after one attempt:"));
}
