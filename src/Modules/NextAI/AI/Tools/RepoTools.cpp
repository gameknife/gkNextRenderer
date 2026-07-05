#include "Modules/NextAI/AI/Tools/RepoTools.hpp"
#include "Modules/NextAI/AI/IAITool.hpp"
#include "Modules/NextAI/AI/ToolRegistry.hpp"
#include "Modules/NextAI/AI/Tools/PathSandbox.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fmt/format.h>
#include <fstream>
#include <memory>
#include <nlohmann/json.hpp>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace NextAI
{
    namespace
    {
        constexpr size_t kMaxResultChars = 24000;
        constexpr uintmax_t kMaxScanFileBytes = 512 * 1024;
        constexpr uintmax_t kMaxReadFileBytes = 2 * 1024 * 1024;

        int ClampInt(int n, int lo, int hi)
        {
            if (n < lo) return lo;
            if (n > hi) return hi;
            return n;
        }

        std::string ArgString(const json& args, const std::string& key, const std::string& fallback)
        {
            if (!args.is_object()) return fallback;
            auto it = args.find(key);
            if (it == args.end()) return fallback;
            if (it->is_string()) return it->get<std::string>();
            return fallback;
        }

        int ArgInt(const json& args, const std::string& key, int fallback)
        {
            if (!args.is_object()) return fallback;
            auto it = args.find(key);
            if (it == args.end()) return fallback;
            if (it->is_number_integer()) return it->get<int>();
            if (it->is_number_float()) return static_cast<int>(it->get<double>());
            if (it->is_string())
            {
                try { return std::stoi(it->get<std::string>()); }
                catch (...) { return fallback; }
            }
            return fallback;
        }

        std::vector<std::string> ArgStringArray(const json& args, const std::string& key)
        {
            std::vector<std::string> out;
            if (!args.is_object()) return out;
            auto it = args.find(key);
            if (it == args.end() || !it->is_array()) return out;
            for (const auto& v : *it)
            {
                if (v.is_string()) out.push_back(v.get<std::string>());
            }
            return out;
        }

        std::string TruncateText(const std::string& s, size_t max)
        {
            if (s.size() <= max) return s;
            return s.substr(0, max) + fmt::format("\n... [truncated to {} chars]", max);
        }

        std::string ToLower(std::string s)
        {
            for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return s;
        }

        // Quoting helper for invoking external commands via std::system / _popen.
        std::string ShellQuote(const std::string& arg)
        {
#ifdef _WIN32
            // Windows: wrap in double quotes, escape embedded quotes.
            std::string out;
            out.reserve(arg.size() + 2);
            out.push_back('"');
            for (char c : arg)
            {
                if (c == '"') out.push_back('\\');
                out.push_back(c);
            }
            out.push_back('"');
            return out;
#else
            std::string out;
            out.reserve(arg.size() + 2);
            out.push_back('\'');
            for (char c : arg)
            {
                if (c == '\'') out += "'\\''";
                else out.push_back(c);
            }
            out.push_back('\'');
            return out;
#endif
        }

        struct FProcResult
        {
            int exitCode = -1;
            std::string stdout_;
            std::string error;
        };

        FProcResult RunCommandCapture(const std::string& cwd, const std::string& cmd,
                                      const std::vector<std::string>& args)
        {
            FProcResult res;
            // Build full command line: cd into cwd && cmd arg1 arg2 ...
            std::string fullCmd;
#ifdef _WIN32
            fullCmd = "cmd /S /C \"";
            fullCmd += "cd /D " + ShellQuote(cwd) + " && ";
            fullCmd += ShellQuote(cmd);
            for (const auto& a : args) { fullCmd += " " + ShellQuote(a); }
            // Redirect stderr to stdout so we capture failure messages too.
            fullCmd += " 2>&1\"";
            std::FILE* pipe = _popen(fullCmd.c_str(), "r");
#else
            fullCmd = "cd " + ShellQuote(cwd) + " && " + ShellQuote(cmd);
            for (const auto& a : args) { fullCmd += " " + ShellQuote(a); }
            fullCmd += " 2>&1";
            std::FILE* pipe = popen(fullCmd.c_str(), "r");
#endif
            if (!pipe)
            {
                res.error = "failed to launch process";
                return res;
            }
            std::array<char, 4096> buf{};
            while (std::fgets(buf.data(), static_cast<int>(buf.size()), pipe) != nullptr)
            {
                res.stdout_.append(buf.data());
                if (res.stdout_.size() > kMaxResultChars * 2) break;
            }
#ifdef _WIN32
            res.exitCode = _pclose(pipe);
#else
            res.exitCode = pclose(pipe);
#endif
            return res;
        }

        std::string GitOutput(const std::string& repoRoot, const std::vector<std::string>& args)
        {
            FProcResult r = RunCommandCapture(repoRoot, "git", args);
            if (r.exitCode != 0)
            {
                return std::string("ERROR: git ") + (args.empty() ? "" : args[0]) + " failed: " + r.stdout_;
            }
            return r.stdout_;
        }

        std::vector<std::string> RepoTrackedFiles(const std::string& repoRoot)
        {
            std::vector<std::string> out;
            FProcResult r = RunCommandCapture(repoRoot, "git", {"ls-files"});
            if (r.exitCode != 0) return out;
            std::istringstream is(r.stdout_);
            std::string line;
            while (std::getline(is, line))
            {
                while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
                {
                    line.pop_back();
                }
                if (!line.empty()) out.push_back(ToSlash(line));
            }
            return out;
        }

        // ---------- Tools ----------

        class FListDirTool : public IAITool
        {
        public:
            explicit FListDirTool(std::string repoRoot) : repoRoot_(std::move(repoRoot)) {}
            std::string Name() const override { return "list_dir"; }
            FToolSchema Schema() const override
            {
                FToolSchema s;
                s.name = "list_dir";
                s.description = "List entries of a directory inside the repository.";
                s.params.push_back({"path", EToolParamType::String, "repo-relative path (default '.')", false});
                s.params.push_back({"max_entries", EToolParamType::Integer, "limit entries (1-200, default 50)", false});
                return s;
            }
            std::string Execute(const json& args, FToolCallContext&) override
            {
                std::string rel = ArgString(args, "path", ".");
                int maxEntries = ClampInt(ArgInt(args, "max_entries", 50), 1, 200);
                auto safe = SafeRepoPath(repoRoot_, rel);
                if (!safe) return "ERROR: path escapes repo: " + rel;
                std::error_code ec;
                if (!fs::exists(*safe, ec)) return "ERROR: not found: " + rel;
                if (!fs::is_directory(*safe, ec)) return "ERROR: not a directory: " + rel;

                std::vector<fs::directory_entry> entries;
                for (const auto& e : fs::directory_iterator(*safe, ec))
                {
                    entries.push_back(e);
                }
                std::sort(entries.begin(), entries.end(),
                          [](const fs::directory_entry& a, const fs::directory_entry& b) {
                              const bool ad = a.is_directory();
                              const bool bd = b.is_directory();
                              if (ad != bd) return ad;
                              return a.path().filename().string() < b.path().filename().string();
                          });
                std::ostringstream os;
                int shown = 0;
                for (const auto& e : entries)
                {
                    if (shown >= maxEntries)
                    {
                        os << "... [" << (entries.size() - shown) << " more entries]\n";
                        break;
                    }
                    os << e.path().filename().string();
                    if (e.is_directory()) os << "/";
                    os << "\n";
                    ++shown;
                }
                return os.str();
            }

        private:
            std::string repoRoot_;
        };

        class FFindFilesTool : public IAITool
        {
        public:
            explicit FFindFilesTool(std::string repoRoot) : repoRoot_(std::move(repoRoot)) {}
            std::string Name() const override { return "find_files"; }
            FToolSchema Schema() const override
            {
                FToolSchema s;
                s.name = "find_files";
                s.description = "Find tracked files / directories whose path contains a substring (case-insensitive).";
                s.params.push_back({"query", EToolParamType::String, "substring to match", true});
                s.params.push_back({"max_results", EToolParamType::Integer, "limit results (1-300, default 80)", false});
                return s;
            }
            std::string Execute(const json& args, FToolCallContext&) override
            {
                std::string q = ToLower(ArgString(args, "query", ""));
                while (!q.empty() && std::isspace(static_cast<unsigned char>(q.back()))) q.pop_back();
                if (q.empty()) return "ERROR: query is required";
                int maxResults = ClampInt(ArgInt(args, "max_results", 80), 1, 300);
                std::vector<std::string> files = RepoTrackedFiles(repoRoot_);
                std::set<std::string> seen;
                std::vector<std::string> matches;
                for (const std::string& file : files)
                {
                    std::string lo = ToLower(file);
                    if (lo.find(q) != std::string::npos && seen.insert(file).second)
                    {
                        matches.push_back(file);
                    }
                    // Walk parent directories.
                    std::string dir = file;
                    size_t slash = dir.find_last_of('/');
                    while (slash != std::string::npos)
                    {
                        dir = dir.substr(0, slash);
                        if (dir.empty()) break;
                        if (ToLower(dir).find(q) != std::string::npos)
                        {
                            std::string dirSlash = dir + "/";
                            if (seen.insert(dirSlash).second) matches.push_back(dirSlash);
                        }
                        slash = dir.find_last_of('/');
                    }
                }
                std::sort(matches.begin(), matches.end());
                if (matches.empty()) return "(no matching files or directories)";
                std::ostringstream os;
                int shown = 0;
                for (const auto& m : matches)
                {
                    if (shown >= maxResults)
                    {
                        os << "... [" << (matches.size() - shown) << " more]\n";
                        break;
                    }
                    os << m << "\n";
                    ++shown;
                }
                return os.str();
            }

        private:
            std::string repoRoot_;
        };

        class FSearchTextTool : public IAITool
        {
        public:
            explicit FSearchTextTool(std::string repoRoot) : repoRoot_(std::move(repoRoot)) {}
            std::string Name() const override { return "search_text"; }
            FToolSchema Schema() const override
            {
                FToolSchema s;
                s.name = "search_text";
                s.description = "Case-insensitive substring search across tracked text files under a path prefix.";
                s.params.push_back({"query", EToolParamType::String, "substring to look for", true});
                s.params.push_back({"path", EToolParamType::String, "repo-relative path prefix (default '.')", false});
                s.params.push_back({"max_results", EToolParamType::Integer, "limit matches (1-300, default 80)", false});
                return s;
            }
            std::string Execute(const json& args, FToolCallContext&) override
            {
                std::string q = ArgString(args, "query", "");
                while (!q.empty() && std::isspace(static_cast<unsigned char>(q.back()))) q.pop_back();
                if (q.empty()) return "ERROR: query is required";
                std::string rel = ArgString(args, "path", ".");
                int maxResults = ClampInt(ArgInt(args, "max_results", 80), 1, 300);

                auto base = SafeRepoPath(repoRoot_, rel);
                if (!base) return "ERROR: path escapes repo: " + rel;
                std::string baseGen = ToSlash(*base);

                std::string qLower = ToLower(q);
                std::vector<std::string> files = RepoTrackedFiles(repoRoot_);
                std::vector<std::string> matches;
                for (const std::string& relFile : files)
                {
                    auto full = SafeRepoPath(repoRoot_, relFile);
                    if (!full) continue;
                    std::string fullGen = ToSlash(*full);
                    if (fullGen != baseGen && fullGen.rfind(baseGen + "/", 0) != 0) continue;
                    std::error_code ec;
                    if (!fs::is_regular_file(*full, ec)) continue;
                    if (fs::file_size(*full, ec) > kMaxScanFileBytes) continue;
                    std::ifstream f(*full, std::ios::binary);
                    if (!f) continue;
                    std::ostringstream buf;
                    buf << f.rdbuf();
                    std::string data = buf.str();
                    if (IsProbablyBinary(data)) continue;

                    size_t lineNo = 1;
                    size_t pos = 0;
                    while (pos < data.size())
                    {
                        size_t nl = data.find('\n', pos);
                        std::string line = data.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
                        if (!line.empty() && line.back() == '\r') line.pop_back();
                        if (ToLower(line).find(qLower) != std::string::npos)
                        {
                            // Trim leading whitespace for compactness.
                            size_t start = line.find_first_not_of(" \t");
                            std::string trimmed = (start == std::string::npos) ? line : line.substr(start);
                            matches.push_back(fmt::format("{}:{}:{}", relFile, lineNo, trimmed));
                            if (static_cast<int>(matches.size()) >= maxResults)
                            {
                                std::ostringstream os;
                                for (const auto& m : matches) os << m << "\n";
                                return os.str();
                            }
                        }
                        if (nl == std::string::npos) break;
                        pos = nl + 1;
                        ++lineNo;
                    }
                }
                if (matches.empty()) return "(no text matches)";
                std::ostringstream os;
                for (const auto& m : matches) os << m << "\n";
                return os.str();
            }

        private:
            std::string repoRoot_;
        };

        struct FSymbolMatch
        {
            std::string kind;
            std::string path;
            int line = 0;
            std::string text;
        };

        bool IsForwardDeclLine(const std::string& trimmed)
        {
            return !trimmed.empty() && trimmed.back() == ';' && trimmed.find('{') == std::string::npos;
        }

        bool MatchesOutsideString(const std::regex& re, const std::string& line)
        {
            std::smatch m;
            if (!std::regex_search(line, m, re)) return false;
            const auto pos = static_cast<size_t>(m.position(0));
            bool inString = false;
            bool escape = false;
            for (size_t i = 0; i < pos; ++i)
            {
                char c = line[i];
                if (escape) { escape = false; continue; }
                if (c == '\\') { escape = true; continue; }
                if (c == '"') inString = !inString;
            }
            return !inString;
        }

        void WriteSymbolSection(std::ostringstream& os, const char* heading,
                                 const std::vector<FSymbolMatch>& items, int max)
        {
            if (items.empty() || max <= 0) return;
            os << "\n[" << heading << "]\n";
            int limit = std::min<int>(static_cast<int>(items.size()), max);
            for (int i = 0; i < limit; ++i)
            {
                const auto& m = items[i];
                os << m.path << ":" << m.line << ":" << m.text << "\n";
            }
            if (static_cast<int>(items.size()) > limit)
            {
                os << "... [" << (items.size() - limit) << " more " << heading << "]\n";
            }
        }

        class FFindSymbolTool : public IAITool
        {
        public:
            explicit FFindSymbolTool(std::string repoRoot) : repoRoot_(std::move(repoRoot)) {}
            std::string Name() const override { return "find_symbol"; }
            FToolSchema Schema() const override
            {
                FToolSchema s;
                s.name = "find_symbol";
                s.description = "Find C/C++ symbol definitions, declarations, implementations, references in tracked source files.";
                s.params.push_back({"symbol", EToolParamType::String, "symbol name (no namespace)", true});
                s.params.push_back({"max_results", EToolParamType::Integer, "limit per group (1-300, default 80)", false});
                return s;
            }
            std::string Execute(const json& args, FToolCallContext&) override
            {
                std::string symbol = ArgString(args, "symbol", "");
                while (!symbol.empty() && std::isspace(static_cast<unsigned char>(symbol.back()))) symbol.pop_back();
                if (symbol.empty()) return "ERROR: symbol is required";
                int maxResults = ClampInt(ArgInt(args, "max_results", 80), 1, 300);

                std::string escaped;
                for (char c : symbol)
                {
                    if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') escaped.push_back(c);
                    else { escaped.push_back('\\'); escaped.push_back(c); }
                }
                std::regex defRe(R"(\b(class|struct|enum\s+class)\s+)" + escaped + R"(\b)");
                std::regex implRe(R"(\b)" + escaped + R"(::[~A-Za-z_])");
                std::regex refRe(R"(\b)" + escaped + R"(\b)");

                std::vector<FSymbolMatch> defs, decls, impls, refs;
                std::vector<std::string> files = RepoTrackedFiles(repoRoot_);
                for (const auto& relFile : files)
                {
                    if (!IsSourceLikePath(relFile)) continue;
                    auto full = SafeRepoPath(repoRoot_, relFile);
                    if (!full) continue;
                    std::error_code ec;
                    if (!fs::is_regular_file(*full, ec)) continue;
                    if (fs::file_size(*full, ec) > kMaxScanFileBytes) continue;
                    std::ifstream f(*full, std::ios::binary);
                    if (!f) continue;
                    std::ostringstream buf;
                    buf << f.rdbuf();
                    std::string data = buf.str();
                    if (IsProbablyBinary(data)) continue;

                    size_t lineNo = 1, pos = 0;
                    while (pos < data.size())
                    {
                        size_t nl = data.find('\n', pos);
                        std::string line = data.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
                        if (!line.empty() && line.back() == '\r') line.pop_back();
                        std::string trimmed = line;
                        size_t start = trimmed.find_first_not_of(" \t");
                        if (start != std::string::npos) trimmed = trimmed.substr(start);
                        size_t end = trimmed.find_last_not_of(" \t");
                        if (end != std::string::npos) trimmed.resize(end + 1);

                        if (!trimmed.empty())
                        {
                            FSymbolMatch m{"", relFile, static_cast<int>(lineNo), trimmed};
                            if (MatchesOutsideString(defRe, line))
                            {
                                if (IsForwardDeclLine(trimmed)) { m.kind = "declaration"; decls.push_back(m); }
                                else { m.kind = "definition"; defs.push_back(m); }
                            }
                            else if (MatchesOutsideString(implRe, line))
                            {
                                m.kind = "implementation"; impls.push_back(m);
                            }
                            else if (MatchesOutsideString(refRe, line))
                            {
                                m.kind = "reference"; refs.push_back(m);
                            }
                        }

                        if (nl == std::string::npos) break;
                        pos = nl + 1;
                        ++lineNo;
                    }
                }

                auto sortMatches = [](std::vector<FSymbolMatch>& v) {
                    std::sort(v.begin(), v.end(), [](const FSymbolMatch& a, const FSymbolMatch& b) {
                        if (a.path != b.path) return a.path < b.path;
                        return a.line < b.line;
                    });
                };
                sortMatches(defs); sortMatches(decls); sortMatches(impls); sortMatches(refs);

                if (defs.empty() && decls.empty() && impls.empty() && refs.empty())
                {
                    return "(no symbol matches)";
                }
                std::ostringstream os;
                os << "symbol: " << symbol << "\n";
                int remaining = maxResults;
                WriteSymbolSection(os, "definitions", defs, remaining);
                remaining -= std::min<int>(static_cast<int>(defs.size()), remaining);
                if (remaining > 0) WriteSymbolSection(os, "declarations", decls, remaining);
                remaining -= std::min<int>(static_cast<int>(decls.size()), remaining);
                if (remaining > 0) WriteSymbolSection(os, "implementations", impls, remaining);
                remaining -= std::min<int>(static_cast<int>(impls.size()), remaining);
                if (remaining > 0) WriteSymbolSection(os, "references", refs, remaining);
                return os.str();
            }

        private:
            std::string repoRoot_;
        };

        class FReadFileTool : public IAITool
        {
        public:
            explicit FReadFileTool(std::string repoRoot) : repoRoot_(std::move(repoRoot)) {}
            std::string Name() const override { return "read_file"; }
            FToolSchema Schema() const override
            {
                FToolSchema s;
                s.name = "read_file";
                s.description = "Read a text file inside the repository (max 2 MiB on disk, output capped).";
                s.params.push_back({"path", EToolParamType::String, "repo-relative file path", true});
                s.params.push_back({"max_chars", EToolParamType::Integer, "limit output chars (256-24000, default 12000)", false});
                return s;
            }
            std::string Execute(const json& args, FToolCallContext&) override
            {
                std::string rel = ArgString(args, "path", "");
                while (!rel.empty() && std::isspace(static_cast<unsigned char>(rel.back()))) rel.pop_back();
                if (rel.empty()) return "ERROR: path is required";
                int maxChars = ClampInt(ArgInt(args, "max_chars", 12000), 256, static_cast<int>(kMaxResultChars));
                auto full = SafeRepoPath(repoRoot_, rel);
                if (!full) return "ERROR: path escapes repo: " + rel;
                std::error_code ec;
                if (!fs::exists(*full, ec)) return "ERROR: not found: " + rel;
                if (fs::is_directory(*full, ec)) return "ERROR: path is a directory: " + rel;
                uintmax_t size = fs::file_size(*full, ec);
                if (size > kMaxReadFileBytes)
                {
                    return fmt::format("ERROR: file too large: {} ({} bytes)", rel, size);
                }
                std::ifstream f(*full, std::ios::binary);
                if (!f) return "ERROR: failed to open: " + rel;
                std::ostringstream buf;
                buf << f.rdbuf();
                std::string data = buf.str();
                if (IsProbablyBinary(data)) return "ERROR: binary file skipped: " + rel;
                return TruncateText(data, static_cast<size_t>(maxChars));
            }

        private:
            std::string repoRoot_;
        };

        class FGitLogTool : public IAITool
        {
        public:
            explicit FGitLogTool(std::string repoRoot) : repoRoot_(std::move(repoRoot)) {}
            std::string Name() const override { return "git_log"; }
            FToolSchema Schema() const override
            {
                FToolSchema s;
                s.name = "git_log";
                s.description = "Read recent git commits (oneline + file status).";
                s.params.push_back({"limit", EToolParamType::Integer, "commits to show (1-30, default 8)", false});
                return s;
            }
            std::string Execute(const json& args, FToolCallContext&) override
            {
                int limit = ClampInt(ArgInt(args, "limit", 8), 1, 30);
                return GitOutput(repoRoot_, {"log", "-n", std::to_string(limit), "--date=short",
                                              "--name-status", "--format=---COMMIT---%n%h %ad %an%n%s"});
            }
        private:
            std::string repoRoot_;
        };

        class FGitShowTool : public IAITool
        {
        public:
            explicit FGitShowTool(std::string repoRoot) : repoRoot_(std::move(repoRoot)) {}
            std::string Name() const override { return "git_show"; }
            FToolSchema Schema() const override
            {
                FToolSchema s;
                s.name = "git_show";
                s.description = "Read a specific git commit (HEAD by default).";
                s.params.push_back({"ref", EToolParamType::String, "git rev (default HEAD)", false});
                s.params.push_back({"max_chars", EToolParamType::Integer, "limit output chars (1000-24000, default 16000)", false});
                return s;
            }
            std::string Execute(const json& args, FToolCallContext&) override
            {
                std::string ref = ArgString(args, "ref", "HEAD");
                if (ref.empty()) ref = "HEAD";
                int maxChars = ClampInt(ArgInt(args, "max_chars", 16000), 1000, static_cast<int>(kMaxResultChars));
                std::string out = GitOutput(repoRoot_, {"show", "--stat", "--name-status",
                                                        "--format=fuller", ref});
                return TruncateText(out, static_cast<size_t>(maxChars));
            }
        private:
            std::string repoRoot_;
        };

        std::string ValidateReadOnlyCommand(const std::string& cmdName, const std::vector<std::string>& args)
        {
            std::string base = cmdName;
            size_t slash = base.find_last_of("/\\");
            if (slash != std::string::npos) base = base.substr(slash + 1);
            base = ToLower(base);

            if (base == "git" || base == "git.exe")
            {
                if (args.empty()) return "git subcommand required";
                static const std::set<std::string> allowed = {
                    "branch", "diff", "log", "show", "status", "rev-parse", "ls-files"
                };
                if (!allowed.count(args[0])) return "git subcommand '" + args[0] + "' is not allowed";
            }
            else if (base == "gnb" || base == "gnb.exe" || base == "gnb.bat")
            {
                if (args.empty() || args[0] != "todo") return "only gnb todo read commands are allowed";
                if (args.size() >= 2 && args[1] != "list" && args[1] != "show" && args[1] != "next")
                {
                    return "gnb todo '" + args[1] + "' is not allowed";
                }
            }
            else
            {
                return "command '" + cmdName + "' is not allowed";
            }

            for (const auto& a : args)
            {
                if (a.find_first_of("\r\n;&|<>") != std::string::npos)
                {
                    return "unsafe shell metacharacter in arg '" + a + "'";
                }
            }
            return {};
        }

        class FRunCmdTool : public IAITool
        {
        public:
            explicit FRunCmdTool(std::string repoRoot) : repoRoot_(std::move(repoRoot)) {}
            std::string Name() const override { return "run_cmd"; }
            FToolSchema Schema() const override
            {
                FToolSchema s;
                s.name = "run_cmd";
                s.description = "Run a whitelisted read-only command (git status/diff/log/show/ls-files/rev-parse/branch, or gnb todo list/show/next).";
                s.params.push_back({"cmd", EToolParamType::String, "command name", true});
                s.params.push_back({"args", EToolParamType::Array, "argv as string array", false});
                return s;
            }
            std::string Execute(const json& args, FToolCallContext&) override
            {
                std::string cmd = ArgString(args, "cmd", "");
                while (!cmd.empty() && std::isspace(static_cast<unsigned char>(cmd.back()))) cmd.pop_back();
                if (cmd.empty()) return "ERROR: cmd is required";
                std::vector<std::string> argv = ArgStringArray(args, "args");
                std::string err = ValidateReadOnlyCommand(cmd, argv);
                if (!err.empty()) return "ERROR: " + err;
                FProcResult r = RunCommandCapture(repoRoot_, cmd, argv);
                if (r.exitCode != 0)
                {
                    return fmt::format("ERROR: command failed (exit={}): {}", r.exitCode, r.stdout_);
                }
                return TruncateText(r.stdout_, kMaxResultChars);
            }
        private:
            std::string repoRoot_;
        };
    }

    void RegisterRepoTools(FToolRegistry& registry, const FRepoToolsOptions& options)
    {
        const std::string& root = options.repoRoot;
        if (options.enableListDir)    registry.Register(std::make_unique<FListDirTool>(root));
        if (options.enableFindFiles)  registry.Register(std::make_unique<FFindFilesTool>(root));
        if (options.enableSearchText) registry.Register(std::make_unique<FSearchTextTool>(root));
        if (options.enableFindSymbol) registry.Register(std::make_unique<FFindSymbolTool>(root));
        if (options.enableReadFile)   registry.Register(std::make_unique<FReadFileTool>(root));
        if (options.enableGit)
        {
            registry.Register(std::make_unique<FGitLogTool>(root));
            registry.Register(std::make_unique<FGitShowTool>(root));
        }
        if (options.enableRunCmd)     registry.Register(std::make_unique<FRunCmdTool>(root));
    }
}
