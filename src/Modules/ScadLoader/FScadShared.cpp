#include "Modules/ScadLoader/FScadShared.h"

#include "Modules/ScadLoader/FScadLexer.h"
#include "Modules/ScadLoader/FScadParser.h"
#include "Engine/Utilities/FileHelper.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <unordered_set>

#include <fmt/format.h>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/matrix_decompose.hpp>

namespace Assets::Scad
{
    namespace
    {
        namespace fs = std::filesystem;

        bool ScadReadFile(const fs::path& path, std::string& out)
        {
            std::vector<uint8_t> data;
            bool loaded = false;
            if (auto* package = Utilities::Package::FPackageFileSystem::TryGetInstance())
            {
                loaded = package->LoadFile(path.generic_string(), data);
            }
            else
            {
                const fs::path loosePath = path.is_absolute()
                    ? path
                    : Utilities::FileHelper::GetRuntimeFilePath(path);
                std::ifstream file(loosePath, std::ios::binary);
                if (file.is_open())
                {
                    data.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
                    loaded = true;
                }
            }
            if (!loaded)
            {
                return false;
            }
            out.assign(reinterpret_cast<const char*>(data.data()), data.size());
            return true;
        }

        bool ScadIsIdentChar(char c)
        {
            return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
        }

        // Extracts `use <path>` / `include <path>` directives and blanks them
        // out of the source (preserving offsets/line numbers) so the lexer
        // never sees them.
        struct ScadDirective
        {
            bool isInclude = false;
            std::string path;
        };

        std::vector<ScadDirective> ScadExtractDirectives(const std::string& source, std::string& stripped)
        {
            std::vector<ScadDirective> directives;
            stripped = source;

            auto tryMatch = [&](size_t i, const char* keyword, bool isInclude) -> size_t
            {
                const size_t kwLen = std::char_traits<char>::length(keyword);
                if (source.compare(i, kwLen, keyword) != 0)
                {
                    return 0;
                }
                if (i > 0 && ScadIsIdentChar(source[i - 1]))
                {
                    return 0; // part of a larger identifier
                }
                size_t j = i + kwLen;
                if (j < source.size() && ScadIsIdentChar(source[j]))
                {
                    return 0;
                }
                while (j < source.size() && (source[j] == ' ' || source[j] == '\t'))
                {
                    ++j;
                }
                if (j >= source.size() || source[j] != '<')
                {
                    return 0;
                }
                const size_t pathStart = j + 1;
                const size_t pathEnd = source.find('>', pathStart);
                if (pathEnd == std::string::npos)
                {
                    return 0;
                }
                ScadDirective d;
                d.isInclude = isInclude;
                d.path = source.substr(pathStart, pathEnd - pathStart);
                directives.push_back(d);
                for (size_t k = i; k <= pathEnd; ++k)
                {
                    if (stripped[k] != '\n')
                    {
                        stripped[k] = ' ';
                    }
                }
                return pathEnd + 1;
            };

            for (size_t i = 0; i < source.size();)
            {
                if (source[i] == '"')
                {
                    ++i;
                    while (i < source.size())
                    {
                        if (source[i] == '\\' && i + 1 < source.size())
                        {
                            i += 2;
                            continue;
                        }
                        if (source[i++] == '"')
                        {
                            break;
                        }
                    }
                    continue;
                }
                if (source[i] == '/' && i + 1 < source.size() && source[i + 1] == '/')
                {
                    i += 2;
                    while (i < source.size() && source[i] != '\n')
                    {
                        ++i;
                    }
                    continue;
                }
                if (source[i] == '/' && i + 1 < source.size() && source[i + 1] == '*')
                {
                    i += 2;
                    while (i + 1 < source.size() && !(source[i] == '*' && source[i + 1] == '/'))
                    {
                        ++i;
                    }
                    i = std::min(i + 2, source.size());
                    continue;
                }

                size_t next = tryMatch(i, "use", false);
                if (next == 0)
                {
                    next = tryMatch(i, "include", true);
                }
                i = (next != 0) ? next : i + 1;
            }
            return directives;
        }

        struct ScadPosKey
        {
            float x, y, z;
            bool operator==(const ScadPosKey& o) const { return x == o.x && y == o.y && z == o.z; }
        };

        struct ScadPosKeyHash
        {
            size_t operator()(const ScadPosKey& k) const
            {
                uint32_t bits[3];
                std::memcpy(&bits[0], &k.x, sizeof(float));
                std::memcpy(&bits[1], &k.y, sizeof(float));
                std::memcpy(&bits[2], &k.z, sizeof(float));
                size_t h = 1469598103934665603ull;
                for (uint32_t b : bits)
                {
                    h ^= static_cast<size_t>(b);
                    h *= 1099511628211ull;
                }
                return h;
            }
        };
    } // namespace

    bool LoadScadProgram(const std::string& filename, ScadProgram& outProgram, std::string& outError)
    {
        fs::path mainPath = filename;
        if (mainPath.is_absolute())
        {
            std::error_code ec;
            const std::array<fs::path, 2> roots = {
                Utilities::FileHelper::GetRuntimeRoot(),
                Utilities::FileHelper::GetWritableRuntimeRoot() / "asset-cache",
            };
            for (const fs::path& root : roots)
            {
                const fs::path relative = fs::relative(mainPath, root, ec);
                if (!ec && !relative.empty() && relative.generic_string().rfind("..", 0) != 0)
                {
                    mainPath = relative.lexically_normal();
                    break;
                }
                ec.clear();
            }
        }
        mainPath = mainPath.lexically_normal();

        std::unordered_set<std::string> parsedDefinitions;
        std::unordered_set<std::string> executedTopLevel;
        struct WorkItem
        {
            fs::path path;
            bool executable = false;
        };
        std::vector<WorkItem> queue;
        queue.push_back({mainPath, true});

        size_t head = 0;
        while (head < queue.size())
        {
            const WorkItem item = queue[head++];
            const std::string key = item.path.string();
            if (item.executable)
            {
                if (executedTopLevel.count(key) != 0)
                {
                    continue;
                }
            }
            else if (parsedDefinitions.count(key) != 0)
            {
                continue;
            }

            std::string source;
            if (!ScadReadFile(item.path, source))
            {
                outError = fmt::format("cannot read referenced file: {}", key);
                return false;
            }

            std::string stripped;
            std::vector<ScadDirective> directives = ScadExtractDirectives(source, stripped);

            std::vector<Token> tokens;
            std::string err;
            if (!ScadLexer::Tokenize(stripped, tokens, err))
            {
                outError = fmt::format("lex error in {}: {}", key, err);
                return false;
            }
            Scope scope;
            if (!ScadParser::Parse(tokens, scope, err))
            {
                outError = fmt::format("parse error in {}: {}", key, err);
                return false;
            }

            parsedDefinitions.insert(key);
            if (item.executable)
            {
                executedTopLevel.insert(key);
            }

            for (const StmtPtr& s : scope)
            {
                if (!s)
                {
                    continue;
                }
                if (s->kind == StmtKind::ModuleDef)
                {
                    outProgram.modules[s->name] = s;
                }
                else if (s->kind == StmtKind::FunctionDef)
                {
                    outProgram.functions[s->name] = s;
                }
                else if (item.executable)
                {
                    outProgram.mainTopLevel.push_back(s);
                }
            }

            const fs::path baseDir = item.path.parent_path();
            std::error_code ec;
            for (const ScadDirective& d : directives)
            {
                fs::path target = (baseDir / d.path).lexically_normal();
                if (target.is_absolute())
                {
                    target = fs::weakly_canonical(target, ec);
                }
                queue.push_back({target, d.isInclude});
            }
        }
        return true;
    }

    glm::vec3 ScadToWorldPos(const glm::dvec3& p, double scale)
    {
        return glm::vec3(
            static_cast<float>(p.x * scale),
            static_cast<float>(p.z * scale),
            static_cast<float>(-p.y * scale));
    }

    glm::dmat4 ScadToWorldBasis(double scale)
    {
        glm::dmat4 basis(1.0);
        basis[0] = glm::dvec4(scale, 0.0, 0.0, 0.0);
        basis[1] = glm::dvec4(0.0, 0.0, -scale, 0.0);
        basis[2] = glm::dvec4(0.0, scale, 0.0, 0.0);
        basis[3] = glm::dvec4(0.0, 0.0, 0.0, 1.0);
        return basis;
    }

    void ScadLocalToEngineTRS(
        const glm::dmat4& scadLocal,
        double scale,
        glm::vec3& outTranslation,
        glm::quat& outRotation,
        glm::vec3& outScale)
    {
        const glm::dmat4 basis = ScadToWorldBasis(scale);
        const glm::dmat4 engineLocal = basis * scadLocal * glm::inverse(basis);

        glm::dvec3 skew(0.0);
        glm::dvec4 perspective(0.0);
        glm::dvec3 scaleD(1.0);
        glm::dvec3 translationD(0.0);
        glm::dquat rotationD(1.0, 0.0, 0.0, 0.0);
        if (!glm::decompose(engineLocal, scaleD, rotationD, translationD, skew, perspective))
        {
            outTranslation = glm::vec3(0.0f);
            outRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
            outScale = glm::vec3(1.0f);
            return;
        }

        outTranslation = glm::vec3(translationD);
        outRotation = glm::normalize(glm::quat(rotationD));
        outScale = glm::vec3(scaleD);
    }

    glm::dmat4 ScadRotateXYZ(const glm::dvec3& degrees)
    {
        glm::dmat4 r(1.0);
        r = glm::rotate(r, degrees.z * kDeg2Rad, glm::dvec3(0, 0, 1));
        r = glm::rotate(r, degrees.y * kDeg2Rad, glm::dvec3(0, 1, 0));
        r = glm::rotate(r, degrees.x * kDeg2Rad, glm::dvec3(1, 0, 0));
        return r;
    }

    std::vector<glm::vec3> ScadComputeSmoothNormals(const std::vector<glm::vec3>& pos, float angleThresholdDeg)
    {
        const size_t triCount = pos.size() / 3;
        if (angleThresholdDeg <= 0.0f)
        {
            std::vector<glm::vec3> out(pos.size(), glm::vec3(0.0f, 1.0f, 0.0f));
            for (size_t t = 0; t < triCount; ++t)
            {
                const glm::vec3 cross =
                    glm::cross(pos[t * 3 + 1] - pos[t * 3 + 0], pos[t * 3 + 2] - pos[t * 3 + 0]);
                const float len = glm::length(cross);
                const glm::vec3 normal = len > 1e-12f ? cross / len : glm::vec3(0.0f, 1.0f, 0.0f);
                out[t * 3 + 0] = normal;
                out[t * 3 + 1] = normal;
                out[t * 3 + 2] = normal;
            }
            return out;
        }

        std::vector<glm::vec3> weighted(triCount, glm::vec3(0.0f)); // raw cross (area weighted)
        std::vector<glm::vec3> unit(triCount, glm::vec3(0.0f, 1.0f, 0.0f));
        for (size_t t = 0; t < triCount; ++t)
        {
            const glm::vec3 cross = glm::cross(pos[t * 3 + 1] - pos[t * 3 + 0], pos[t * 3 + 2] - pos[t * 3 + 0]);
            weighted[t] = cross;
            const float len = glm::length(cross);
            if (len > 1e-12f) unit[t] = cross / len;
        }

        std::unordered_map<ScadPosKey, std::vector<uint32_t>, ScadPosKeyHash> adjacency;
        adjacency.reserve(pos.size());
        for (size_t i = 0; i < pos.size(); ++i)
        {
            adjacency[ScadPosKey{pos[i].x, pos[i].y, pos[i].z}].push_back(static_cast<uint32_t>(i / 3));
        }

        const float cosThreshold = std::cos(angleThresholdDeg * 3.14159265358979323846f / 180.0f);
        std::vector<glm::vec3> out(pos.size(), glm::vec3(0.0f, 1.0f, 0.0f));
        for (size_t i = 0; i < pos.size(); ++i)
        {
            const uint32_t face = static_cast<uint32_t>(i / 3);
            const glm::vec3 fn = unit[face];
            glm::vec3 acc(0.0f);
            for (uint32_t other : adjacency[ScadPosKey{pos[i].x, pos[i].y, pos[i].z}])
            {
                if (glm::dot(unit[other], fn) >= cosThreshold)
                {
                    acc += weighted[other];
                }
            }
            const float len = glm::length(acc);
            out[i] = (len > 1e-12f) ? acc / len : fn;
        }
        return out;
    }
} // namespace Assets::Scad
