#include <catch2/catch_all.hpp>

#include "Assets/Loaders/FLDrawParser.h"

#include <chrono>
#include <filesystem>
#include <fstream>

namespace
{
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
