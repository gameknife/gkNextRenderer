#include <catch2/catch_all.hpp>
#include <nlohmann/json.hpp>

#include "Application/Editor/ScadStudio/ScadSessionStore.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>

using namespace ScadStudio;

namespace
{
    struct FScopedTempDir
    {
        std::filesystem::path path;

        FScopedTempDir()
        {
            path = std::filesystem::temp_directory_path() /
                ("gknext_scad_session_store_" +
                 std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            std::filesystem::create_directories(path);
        }

        ~FScopedTempDir()
        {
            std::error_code ec;
            std::filesystem::remove_all(path, ec);
        }
    };
}

TEST_CASE("ScadSessionStore save tolerates split UTF-8 titles", "[Unit][SCAD][Studio]")
{
    FScopedTempDir tempDir;
    ScadSessionStore store(tempDir.path);

    const std::string prompt =
        "生成一个现代城市，有住宅区，商业区，工厂区。住宅区和商业区普遍高楼林立，城市中有中央公园。有交通路网，有河流。";

    FScadSession session;
    session.id = "model_0001";
    session.title = prompt.substr(0, 28) + "..."; // Mirrors the old byte-splitting bug.
    session.createdAt = 1;
    session.updatedAt = 1;

    FChatTurn turn;
    turn.role = "user";
    turn.content = prompt;
    session.turns.push_back(turn);

    REQUIRE_NOTHROW(store.SaveSession(session));
    REQUIRE_NOTHROW(store.SaveIndex({session}));

    std::ifstream sessionFile(store.WorkspaceDir() / "model_0001.json", std::ios::binary);
    REQUIRE(sessionFile.good());
    nlohmann::json sessionDoc;
    REQUIRE_NOTHROW(sessionFile >> sessionDoc);
    REQUIRE(sessionDoc.value("title", "").starts_with("生成一个"));

    std::ifstream indexFile(store.WorkspaceDir() / "sessions.json", std::ios::binary);
    REQUIRE(indexFile.good());
    nlohmann::json indexDoc;
    REQUIRE_NOTHROW(indexFile >> indexDoc);
    REQUIRE(indexDoc.contains("sessions"));
    REQUIRE(indexDoc["sessions"].size() == 1);
}
