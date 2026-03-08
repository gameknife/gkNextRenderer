#include <catch2/catch_all.hpp>

#include "Assets/Loaders/FLDrawConfig.h"

#include <chrono>
#include <filesystem>
#include <fstream>

namespace
{
    class ScopedLDrawConfigFile
    {
    public:
        explicit ScopedLDrawConfigFile(const std::string& name)
        {
            auto uniqueSuffix = std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count());
            root_ = std::filesystem::temp_directory_path() / (name + "_" + uniqueSuffix);
            std::filesystem::create_directories(root_);
        }

        ~ScopedLDrawConfigFile()
        {
            std::error_code ec;
            std::filesystem::remove_all(root_, ec);
        }

        std::filesystem::path Write(const std::string& contents) const
        {
            std::filesystem::path fullPath = root_ / "LDConfig.ldr";
            std::ofstream out(fullPath);
            REQUIRE(out.is_open());
            out << contents;
            return fullPath;
        }

    private:
        std::filesystem::path root_;
    };

    void CheckVec3Near(const glm::vec3& actual, const glm::vec3& expected)
    {
        CHECK(actual.x == Catch::Approx(expected.x).margin(1e-6f));
        CHECK(actual.y == Catch::Approx(expected.y).margin(1e-6f));
        CHECK(actual.z == Catch::Approx(expected.z).margin(1e-6f));
    }
}

TEST_CASE("LDraw color table applies realistic LGEO overrides", "[Unit][LDraw]")
{
    ScopedLDrawConfigFile config("ldraw_config_realistic");
    std::filesystem::path filePath = config.Write(
        "0 !COLOUR Blue CODE 1 VALUE #0055BF EDGE #333333\n");

    Assets::LDrawColorTable colorTable;
    colorTable.Parse(filePath.string());

    const Assets::LDrawColor* color = colorTable.GetColor(1);
    REQUIRE(color != nullptr);

    const glm::vec3 expectedSrgb(13.0f / 255.0f, 105.0f / 255.0f, 171.0f / 255.0f);
    const glm::vec3 expectedLinear = Assets::LDrawColorTable::SrgbToLinear(expectedSrgb);
    CheckVec3Near(color->diffuse, expectedLinear);
}

TEST_CASE("LDraw color table parses secondary material color", "[Unit][LDraw]")
{
    ScopedLDrawConfigFile config("ldraw_config_secondary");
    std::filesystem::path filePath = config.Write(
        "0 !COLOUR Glitter_Trans_Clear CODE 117 VALUE #FFFFFF EDGE #333333 ALPHA 128 MATERIAL GLITTER VALUE #898788 FRACTION 0.17 MINSIZE 0.02 MAXSIZE 0.1\n");

    Assets::LDrawColorTable colorTable;
    colorTable.Parse(filePath.string());

    const Assets::LDrawColor* color = colorTable.GetColor(117);
    REQUIRE(color != nullptr);
    CHECK(color->finish == Assets::LDrawColor::Finish::Glitter);
    CHECK(color->hasSecondaryDiffuse);

    const glm::vec3 expectedSrgb(0x89 / 255.0f, 0x87 / 255.0f, 0x88 / 255.0f);
    const glm::vec3 expectedLinear = Assets::LDrawColorTable::SrgbToLinear(expectedSrgb);
    CheckVec3Near(color->secondaryDiffuse, expectedLinear);
}
