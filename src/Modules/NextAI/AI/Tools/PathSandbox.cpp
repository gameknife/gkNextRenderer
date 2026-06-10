#include "Modules/NextAI/AI/Tools/PathSandbox.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <system_error>

namespace fs = std::filesystem;

namespace NextAI
{
    std::string ToSlash(std::string path)
    {
        std::replace(path.begin(), path.end(), '\\', '/');
        return path;
    }

    static std::string ToHostPath(const std::string& s)
    {
        std::string out = s;
#ifdef _WIN32
        std::replace(out.begin(), out.end(), '/', '\\');
#endif
        return out;
    }

    std::optional<std::string> SafeRepoPath(const std::string& repoRoot, const std::string& rel)
    {
        try
        {
            std::string relHost = ToHostPath(rel);
            fs::path relPath(relHost);

            // Lexically normalize: collapses ".." but does not access the disk.
            fs::path lexRel = relPath.lexically_normal();
            // Reject inputs that escape the root via .. or absolute paths.
            if (lexRel.is_absolute())
            {
                return std::nullopt;
            }
            std::string lexStr = lexRel.generic_string();
            if (lexStr == ".." || lexStr.rfind("../", 0) == 0)
            {
                return std::nullopt;
            }

            fs::path rootAbs = fs::absolute(fs::path(repoRoot));
            rootAbs = rootAbs.lexically_normal();
            fs::path full = (rootAbs / lexRel).lexically_normal();

            // Ensure the resolved path is at or below rootAbs by string prefix
            // comparison on generic (forward-slash) strings.
            std::string rootGen = rootAbs.generic_string();
            std::string fullGen = full.generic_string();
            // Normalize trailing slash on root for prefix check.
            if (!rootGen.empty() && rootGen.back() == '/')
            {
                rootGen.pop_back();
            }
            if (fullGen != rootGen && fullGen.rfind(rootGen + "/", 0) != 0)
            {
                return std::nullopt;
            }
            return full.string();
        }
        catch (const std::exception&)
        {
            return std::nullopt;
        }
    }

    bool IsProbablyBinary(const std::string& data)
    {
        if (data.empty()) return false;
        size_t limit = data.size();
        if (limit > 4096) limit = 4096;
        for (size_t i = 0; i < limit; ++i)
        {
            if (data[i] == '\0') return true;
        }
        return false;
    }

    bool IsSourceLikePath(std::string_view path)
    {
        size_t dot = path.find_last_of('.');
        if (dot == std::string_view::npos) return false;
        std::string ext(path.substr(dot));
        for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return ext == ".c" || ext == ".cc" || ext == ".cpp" || ext == ".cxx"
            || ext == ".h" || ext == ".hh" || ext == ".hpp" || ext == ".hxx"
            || ext == ".inl" || ext == ".ipp" || ext == ".mm" || ext == ".m";
    }
}
