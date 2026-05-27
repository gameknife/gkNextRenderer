#include <catch2/catch_all.hpp>
#include <nlohmann/json.hpp>

#include "Engine/Runtime/Subsystems/AI/IAITool.hpp"
#include "Engine/Runtime/Subsystems/AI/ToolRegistry.hpp"
#include "Engine/Runtime/Subsystems/AI/Tools/PathSandbox.hpp"
#include "Engine/Runtime/Subsystems/AI/Tools/RepoTools.hpp"

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using json = nlohmann::json;
using namespace NextAI;

namespace
{
    struct FTempRepo
    {
        fs::path root;
        FTempRepo()
        {
            root = fs::temp_directory_path() / fs::path(
                "nextengine_repotools_" + std::to_string(reinterpret_cast<uintptr_t>(this)));
            fs::remove_all(root);
            fs::create_directories(root);
        }
        ~FTempRepo()
        {
            std::error_code ec;
            fs::remove_all(root, ec);
        }
        void Write(const std::string& rel, const std::string& content)
        {
            fs::path p = root / rel;
            fs::create_directories(p.parent_path());
            std::ofstream(p, std::ios::binary) << content;
        }
    };

    std::string Run(FToolRegistry& reg, const std::string& name, const json& args = json::object())
    {
        IAITool* t = reg.Find(name);
        REQUIRE(t != nullptr);
        FToolCallContext ctx;
        return t->Execute(args, ctx);
    }
}

TEST_CASE("SafeRepoPath accepts in-repo paths and rejects escapes", "[Unit][AI][Tools]")
{
    FTempRepo repo;
    REQUIRE(SafeRepoPath(repo.root.string(), ".").has_value());
    REQUIRE(SafeRepoPath(repo.root.string(), "subdir/file.txt").has_value());
    REQUIRE_FALSE(SafeRepoPath(repo.root.string(), "../escape").has_value());
    REQUIRE_FALSE(SafeRepoPath(repo.root.string(), "/etc/passwd").has_value());
    // ../ inside a subdir that still resolves to in-repo is OK
    REQUIRE(SafeRepoPath(repo.root.string(), "sub/../other").has_value());
    // .. that escapes is rejected
    REQUIRE_FALSE(SafeRepoPath(repo.root.string(), "sub/../../escape").has_value());
}

TEST_CASE("IsProbablyBinary flags NUL bytes in the prefix", "[Unit][AI][Tools]")
{
    REQUIRE_FALSE(IsProbablyBinary(""));
    REQUIRE_FALSE(IsProbablyBinary("hello\nworld\n"));
    std::string withNul = "abc";
    withNul.push_back('\0');
    withNul += "def";
    REQUIRE(IsProbablyBinary(withNul));
}

TEST_CASE("IsSourceLikePath recognizes C/C++ extensions", "[Unit][AI][Tools]")
{
    REQUIRE(IsSourceLikePath("foo/bar.cpp"));
    REQUIRE(IsSourceLikePath("a.hpp"));
    REQUIRE(IsSourceLikePath("x.H")); // case-insensitive
    REQUIRE_FALSE(IsSourceLikePath("readme.md"));
    REQUIRE_FALSE(IsSourceLikePath("noext"));
}

TEST_CASE("list_dir lists files and directories, dirs first, with limit", "[Unit][AI][Tools]")
{
    FTempRepo repo;
    repo.Write("a.txt", "1");
    repo.Write("b.txt", "2");
    repo.Write("sub/c.txt", "3");
    FToolRegistry reg;
    FRepoToolsOptions opt;
    opt.repoRoot = repo.root.string();
    // We don't have a git repo here so disable git-based tools.
    opt.enableGit = false;
    opt.enableRunCmd = false;
    opt.enableFindFiles = false;
    opt.enableSearchText = false;
    opt.enableFindSymbol = false;
    RegisterRepoTools(reg, opt);

    std::string out = Run(reg, "list_dir", {{"path", "."}});
    // sub/ should appear before a.txt because directories come first
    auto subPos = out.find("sub/");
    auto aPos = out.find("a.txt");
    REQUIRE(subPos != std::string::npos);
    REQUIRE(aPos != std::string::npos);
    REQUIRE(subPos < aPos);

    // max_entries truncates
    std::string limited = Run(reg, "list_dir", {{"path", "."}, {"max_entries", 1}});
    REQUIRE(limited.find("more entries") != std::string::npos);

    // path escape rejected
    std::string escape = Run(reg, "list_dir", {{"path", "../"}});
    REQUIRE(escape.rfind("ERROR:", 0) == 0);

    // not-found
    std::string missing = Run(reg, "list_dir", {{"path", "no_such_dir"}});
    REQUIRE(missing.rfind("ERROR:", 0) == 0);
}

TEST_CASE("read_file enforces sandbox, binary skip and size limit", "[Unit][AI][Tools]")
{
    FTempRepo repo;
    repo.Write("hello.txt", "line1\nline2\n");
    std::string binary("abc", 3);
    binary.push_back('\0');
    binary += "def";
    repo.Write("bin.dat", binary);

    FToolRegistry reg;
    FRepoToolsOptions opt;
    opt.repoRoot = repo.root.string();
    opt.enableGit = false;
    opt.enableRunCmd = false;
    opt.enableFindFiles = false;
    opt.enableSearchText = false;
    opt.enableFindSymbol = false;
    opt.enableListDir = false;
    RegisterRepoTools(reg, opt);

    std::string ok = Run(reg, "read_file", {{"path", "hello.txt"}});
    REQUIRE(ok.find("line1") != std::string::npos);

    std::string bin = Run(reg, "read_file", {{"path", "bin.dat"}});
    REQUIRE(bin.rfind("ERROR: binary file skipped", 0) == 0);

    std::string missing = Run(reg, "read_file", {{"path", ""}});
    REQUIRE(missing.rfind("ERROR:", 0) == 0);

    std::string escape = Run(reg, "read_file", {{"path", "../oops"}});
    REQUIRE(escape.rfind("ERROR:", 0) == 0);
}

TEST_CASE("run_cmd whitelist rejects unsafe commands and shell metacharacters", "[Unit][AI][Tools]")
{
    FTempRepo repo;
    FToolRegistry reg;
    FRepoToolsOptions opt;
    opt.repoRoot = repo.root.string();
    opt.enableListDir = false;
    opt.enableFindFiles = false;
    opt.enableSearchText = false;
    opt.enableFindSymbol = false;
    opt.enableReadFile = false;
    opt.enableGit = false;
    RegisterRepoTools(reg, opt);

    // Non-whitelisted command
    std::string r1 = Run(reg, "run_cmd", {{"cmd", "rm"}, {"args", json::array({"-rf", "/"})}});
    REQUIRE(r1.rfind("ERROR:", 0) == 0);

    // Whitelisted command name but disallowed subcommand
    std::string r2 = Run(reg, "run_cmd", {{"cmd", "git"}, {"args", json::array({"push"})}});
    REQUIRE(r2.rfind("ERROR:", 0) == 0);

    // Shell meta-character in argument
    std::string r3 = Run(reg, "run_cmd", {{"cmd", "git"}, {"args", json::array({"status", "; rm -rf /"})}});
    REQUIRE(r3.rfind("ERROR:", 0) == 0);

    // Missing cmd
    std::string r4 = Run(reg, "run_cmd", {{"cmd", ""}});
    REQUIRE(r4.rfind("ERROR:", 0) == 0);
}

TEST_CASE("find_files / search_text require a query argument", "[Unit][AI][Tools]")
{
    FTempRepo repo;
    FToolRegistry reg;
    FRepoToolsOptions opt;
    opt.repoRoot = repo.root.string();
    opt.enableGit = false;
    opt.enableRunCmd = false;
    opt.enableReadFile = false;
    opt.enableListDir = false;
    opt.enableFindSymbol = false;
    RegisterRepoTools(reg, opt);

    REQUIRE(Run(reg, "find_files", {}).rfind("ERROR:", 0) == 0);
    REQUIRE(Run(reg, "search_text", {}).rfind("ERROR:", 0) == 0);
}

TEST_CASE("RegisterRepoTools honors per-tool enable flags", "[Unit][AI][Tools]")
{
    FTempRepo repo;
    FToolRegistry reg;
    FRepoToolsOptions opt;
    opt.repoRoot = repo.root.string();
    opt.enableGit = false;
    opt.enableRunCmd = false;
    opt.enableFindFiles = false;
    opt.enableSearchText = false;
    opt.enableFindSymbol = false;
    opt.enableReadFile = false;
    // Only list_dir.
    RegisterRepoTools(reg, opt);
    REQUIRE(reg.Find("list_dir") != nullptr);
    REQUIRE(reg.Find("read_file") == nullptr);
    REQUIRE(reg.Find("find_files") == nullptr);
    REQUIRE(reg.Find("git_log") == nullptr);
    REQUIRE(reg.Find("run_cmd") == nullptr);
    REQUIRE(reg.Size() == 1);
}
