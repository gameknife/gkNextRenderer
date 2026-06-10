#include <catch2/catch_all.hpp>

#include "Modules/LDrawLoader/FLDrawParser.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

#include "Engine/Utilities/FileHelper.hpp"

namespace
{
    uint64_t HashBuffer(const uint8_t* data, size_t size)
    {
        constexpr uint64_t fnvOffset = 1469598103934665603ull;
        constexpr uint64_t fnvPrime = 1099511628211ull;

        uint64_t hash = fnvOffset;
        for (size_t i = 0; i < size; ++i)
        {
            hash ^= static_cast<uint64_t>(data[i]);
            hash *= fnvPrime;
        }

        return hash;
    }

    std::string BuildPartCachePath(const std::filesystem::path& filepath, bool expandSubfiles = true)
    {
        const std::string normalized = std::filesystem::absolute(filepath).lexically_normal().generic_string();
        const uint64_t hash = HashBuffer(
            reinterpret_cast<const uint8_t*>(normalized.data()),
            normalized.size());

        return Utilities::CookHelper::GetCookedFileName(
            fmt::format("{:016x}", hash),
            expandSubfiles ? "ldpart" : "ldpartd");
    }

    class ScopedLDrawTestLibrary
    {
    public:
        explicit ScopedLDrawTestLibrary(const std::string& name)
        {
            auto uniqueSuffix = std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count());
            root_ = std::filesystem::current_path() / (name + "_" + uniqueSuffix);
            std::filesystem::create_directories(root_ / "parts");
            std::filesystem::create_directories(root_ / "p");
        }

        ~ScopedLDrawTestLibrary()
        {
            std::error_code ec;
            std::filesystem::remove_all(root_, ec);
        }

        void WritePart(const std::string& relativePath, const std::string& contents) const
        {
            std::filesystem::path fullPath = root_ / relativePath;
            std::filesystem::create_directories(fullPath.parent_path());

            std::ofstream out(fullPath);
            REQUIRE(out.is_open());
            out << contents;
        }

        std::filesystem::path Root() const
        {
            return root_;
        }

    private:
        std::filesystem::path root_;
    };

    glm::vec3 FaceNormal(const Assets::LDrawFace& face)
    {
        return glm::cross(face.vertices[1] - face.vertices[0], face.vertices[2] - face.vertices[0]);
    }
}

TEST_CASE("LDraw parser preserves mirrored child winding across child BFC certify", "[Unit][LDraw]")
{
    ScopedLDrawTestLibrary lib("ldraw_parser_mirror_test");
    lib.WritePart("parts/child.dat",
                  "0 Child\n"
                  "0 BFC CERTIFY CCW\n"
                  "3 16 0 0 0 1 0 0 0 1 0\n");
    lib.WritePart("parts/top.dat",
                  "0 Top\n"
                  "0 BFC CERTIFY CCW\n"
                  "1 16 0 0 0 -1 0 0 0 1 0 0 0 1 child.dat\n");

    Assets::LDrawColorTable colors;
    Assets::LDrawFileResolver resolver;
    resolver.BuildIndex((lib.Root().string() + "/"));

    Assets::LDrawParser parser(colors, resolver);
    Assets::LDrawPartTemplate tmpl = parser.ParseFile((lib.Root() / "parts" / "top.dat").string());

    REQUIRE(tmpl.faces.size() == 1);
    CHECK(FaceNormal(tmpl.faces[0]).z > 0.0f);
}

TEST_CASE("LDraw parser preserves INVERTNEXT across child BFC certify", "[Unit][LDraw]")
{
    ScopedLDrawTestLibrary lib("ldraw_parser_invertnext_test");
    lib.WritePart("parts/child.dat",
                  "0 Child\n"
                  "0 BFC CERTIFY CCW\n"
                  "3 16 0 0 0 1 0 0 0 1 0\n");
    lib.WritePart("parts/top.dat",
                  "0 Top\n"
                  "0 BFC CERTIFY CCW\n"
                  "0 BFC INVERTNEXT\n"
                  "1 16 0 0 0 1 0 0 0 1 0 0 0 1 child.dat\n");

    Assets::LDrawColorTable colors;
    Assets::LDrawFileResolver resolver;
    resolver.BuildIndex((lib.Root().string() + "/"));

    Assets::LDrawParser parser(colors, resolver);
    Assets::LDrawPartTemplate tmpl = parser.ParseFile((lib.Root() / "parts" / "top.dat").string());

    REQUIRE(tmpl.faces.size() == 1);
    CHECK(FaceNormal(tmpl.faces[0]).z < 0.0f);
}

TEST_CASE("LDraw parser persists part cache and invalidates parent cache when dependency changes", "[Unit][LDraw][Cache]")
{
    ScopedLDrawTestLibrary lib("ldraw_parser_disk_cache_test");
    lib.WritePart("parts/child.dat",
                  "0 Child\n"
                  "0 BFC CERTIFY CCW\n"
                  "3 16 0 0 0 1 0 0 0 1 0\n");
    lib.WritePart("parts/top.dat",
                  "0 Top\n"
                  "0 BFC CERTIFY CCW\n"
                  "1 16 0 0 0 1 0 0 0 1 0 0 0 1 child.dat\n");

    Assets::LDrawColorTable colors;
    Assets::LDrawFileResolver resolver;
    resolver.BuildIndex((lib.Root().string() + "/"));

    const std::filesystem::path topPath = lib.Root() / "parts" / "top.dat";
    const std::filesystem::path childPath = lib.Root() / "parts" / "child.dat";
    const std::string topCachePath = BuildPartCachePath(topPath);
    const std::string childCachePath = BuildPartCachePath(childPath);

    {
        Assets::LDrawParser parser(colors, resolver);
        Assets::LDrawPartTemplate tmpl = parser.ParseFile(topPath.string());
        REQUIRE(tmpl.faces.size() == 1);
        CHECK(FaceNormal(tmpl.faces[0]).z > 0.0f);
    }

    CHECK(std::filesystem::exists(topCachePath));
    CHECK(std::filesystem::exists(childCachePath));

    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    lib.WritePart("parts/child.dat",
                  "0 Child\n"
                  "0 BFC CERTIFY CCW\n"
                  "3 16 0 0 0 0 1 0 1 0 0\n");

    Assets::LDrawParser reparsedParser(colors, resolver);
    Assets::LDrawPartTemplate reparsed = reparsedParser.ParseFile(topPath.string());

    REQUIRE(reparsed.faces.size() == 1);
    CHECK(FaceNormal(reparsed.faces[0]).z < 0.0f);

    std::error_code ec;
    std::filesystem::remove(topCachePath, ec);
    ec.clear();
    std::filesystem::remove(childCachePath, ec);
}
