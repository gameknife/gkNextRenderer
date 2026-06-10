#include <catch2/catch_all.hpp>

#include "Modules/NextAI/AI/LlamaPidFile.hpp"

using namespace NextAI;

TEST_CASE("ParseLlamaPidFile parses well-formed gnb pid file", "[Unit][AI][LlamaPid]")
{
    std::string content =
        "22112\n"
        "127.0.0.1\n"
        "8765\n"
        "gemma-4-E4B-it-Q4_K_M\n"
        "ctx:131072\n"
        "parallel:1\n"
        "reasoning:auto\n";
    FLlamaPidInfo info = ParseLlamaPidFile(content);
    REQUIRE(info.valid);
    REQUIRE(info.pid == 22112);
    REQUIRE(info.host == "127.0.0.1");
    REQUIRE(info.port == 8765);
    REQUIRE(info.model == "gemma-4-E4B-it-Q4_K_M");
    REQUIRE(info.Endpoint() == "http://127.0.0.1:8765");
}

TEST_CASE("ParseLlamaPidFile tolerates CRLF line endings", "[Unit][AI][LlamaPid]")
{
    std::string content = "100\r\nlocalhost\r\n9000\r\nmymodel\r\n";
    FLlamaPidInfo info = ParseLlamaPidFile(content);
    REQUIRE(info.valid);
    REQUIRE(info.host == "localhost");
    REQUIRE(info.port == 9000);
    REQUIRE(info.model == "mymodel");
}

TEST_CASE("ParseLlamaPidFile rejects partial / malformed content", "[Unit][AI][LlamaPid]")
{
    REQUIRE_FALSE(ParseLlamaPidFile("").valid);
    REQUIRE_FALSE(ParseLlamaPidFile("not_a_pid\n127.0.0.1\n8765\nmodel\n").valid);
    REQUIRE_FALSE(ParseLlamaPidFile("123\n127.0.0.1\nNaN\nmodel\n").valid);
    // Partial write (only 2 lines)
    REQUIRE_FALSE(ParseLlamaPidFile("123\n127.0.0.1\n").valid);
    // Empty model
    REQUIRE_FALSE(ParseLlamaPidFile("123\nhost\n8000\n\n").valid);
    // Negative pid
    REQUIRE_FALSE(ParseLlamaPidFile("-1\nhost\n8000\nmodel\n").valid);
}

TEST_CASE("ReadLlamaPidFile returns invalid info when file missing", "[Unit][AI][LlamaPid]")
{
    FLlamaPidInfo info = ReadLlamaPidFile("definitely/does/not/exist/server.pid");
    REQUIRE_FALSE(info.valid);
}
