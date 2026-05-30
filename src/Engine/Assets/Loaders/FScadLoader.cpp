#include "Engine/Assets/Loaders/FScadLoader.h"

#include "Engine/Assets/Core/Node.h"
#include "Engine/Assets/Data/Material.hpp"
#include "Engine/Assets/Loaders/FProcModel.h"
#include "Engine/Assets/Loaders/FScadEvaluator.h"
#include "Engine/Assets/Loaders/FScadLexer.h"
#include "Engine/Assets/Loaders/FScadParser.h"
#include "Engine/Assets/Loaders/FSceneLoader.h"
#include "Engine/Runtime/Components/RenderComponent.h"
#include "Engine/Utilities/FileHelper.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#include <fmt/format.h>
#include <spdlog/spdlog.h>

namespace Assets
{
    namespace
    {
        namespace fs = std::filesystem;

        bool ScadReadFile(const fs::path& path, std::string& out)
        {
            std::ifstream file(path, std::ios::binary);
            if (!file.is_open())
            {
                return false;
            }
            std::ostringstream ss;
            ss << file.rdbuf();
            out = ss.str();
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

        // Converts SCAD (Z-up) space to engine (Y-up) space with uniform scale.
        glm::vec3 ScadToWorldPos(const glm::dvec3& p, double scale)
        {
            return glm::vec3(
                static_cast<float>(p.x * scale),
                static_cast<float>(p.z * scale),
                static_cast<float>(-p.y * scale));
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

        // Per-corner normals for a triangle soup: averages adjacent face normals
        // (area weighted) only across edges whose dihedral stays within the angle
        // threshold, so curved primitives smooth out while box/cap edges stay hard.
        std::vector<glm::vec3> ScadComputeSmoothNormals(const std::vector<glm::vec3>& pos, float angleThresholdDeg)
        {
            const size_t triCount = pos.size() / 3;
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
    } // namespace

    bool FScadLoader::LoadScadScene(
        const std::string& filename,
        EnvironmentSetting& cameraInit,
        std::vector<std::shared_ptr<Node>>& nodes,
        std::vector<Model>& models,
        std::vector<FMaterial>& materials,
        std::vector<LightObject>& /*lights*/,
        std::vector<AnimationTrack>& /*tracks*/,
        std::vector<Skeleton>& /*skeletons*/,
        const ScadLoadOptions& options)
    {
        fs::path mainPath = filename;
        if (!mainPath.is_absolute())
        {
            mainPath = fs::path(Utilities::FileHelper::GetPlatformFilePath(filename.c_str()));
        }

        std::error_code ec;
        if (!fs::exists(mainPath, ec))
        {
            SPDLOG_ERROR("SCAD: file not found: {}", mainPath.string());
            return false;
        }

        // ---- Resolve the use/include closure ----
        std::unordered_map<std::string, scad::StmtPtr> moduleTable;
        std::unordered_map<std::string, scad::StmtPtr> functionTable;
        scad::Scope mainTopLevel;

        std::unordered_set<std::string> parsedDefinitions;
        std::unordered_set<std::string> executedTopLevel;
        struct WorkItem
        {
            fs::path path;
            bool executable = false;
        };
        std::vector<WorkItem> queue;
        queue.push_back({fs::weakly_canonical(mainPath, ec), true});

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
                SPDLOG_ERROR("SCAD: cannot read referenced file: {}", key);
                return false;
            }

            std::string stripped;
            std::vector<ScadDirective> directives = ScadExtractDirectives(source, stripped);

            std::vector<scad::Token> tokens;
            std::string err;
            if (!scad::ScadLexer::Tokenize(stripped, tokens, err))
            {
                SPDLOG_ERROR("SCAD: lex error in {}: {}", key, err);
                return false;
            }
            scad::Scope scope;
            if (!scad::ScadParser::Parse(tokens, scope, err))
            {
                SPDLOG_ERROR("SCAD: parse error in {}: {}", key, err);
                return false;
            }

            parsedDefinitions.insert(key);
            if (item.executable)
            {
                executedTopLevel.insert(key);
            }

            for (const scad::StmtPtr& s : scope)
            {
                if (!s)
                {
                    continue;
                }
                if (s->kind == scad::StmtKind::ModuleDef)
                {
                    moduleTable[s->name] = s;
                }
                else if (s->kind == scad::StmtKind::FunctionDef)
                {
                    functionTable[s->name] = s;
                }
                else if (item.executable)
                {
                    mainTopLevel.push_back(s);
                }
            }

            const fs::path baseDir = item.path.parent_path();
            for (const ScadDirective& d : directives)
            {
                fs::path target = fs::weakly_canonical(baseDir / d.path, ec);
                queue.push_back({target, d.isInclude});
            }
        }

        // ---- Evaluate ----
        scad::EvalResult result;
        std::string evalErr;
        scad::ScadEvaluator::Evaluate(mainTopLevel, moduleTable, functionTable, options, result, evalErr);

        if (result.buckets.empty())
        {
            SPDLOG_WARN("SCAD: no geometry produced from {}", filename);
        }

        const double scale = static_cast<double>(options.scadToWorldScale > 0.0f ? options.scadToWorldScale : 1.0f);

        // ---- Build one Model + Node per color bucket ----
        size_t bucketIndex = 0;
        for (const auto& entry : result.buckets)
        {
            const scad::ColorBucket& bucket = entry.second;
            const size_t triCount = bucket.tris.size() / 3;
            if (triCount == 0)
            {
                continue;
            }

            std::vector<glm::vec3> worldPos(triCount * 3);
            for (size_t i = 0; i < worldPos.size(); ++i)
            {
                worldPos[i] = ScadToWorldPos(bucket.tris[i], scale);
            }
            const std::vector<glm::vec3> normals = ScadComputeSmoothNormals(worldPos, options.smoothAngleDegrees);

            std::vector<Vertex> vertices;
            std::vector<uint32_t> indices;
            vertices.reserve(worldPos.size());
            indices.reserve(worldPos.size());
            for (uint32_t i = 0; i < static_cast<uint32_t>(worldPos.size()); ++i)
            {
                vertices.push_back(Vertex{worldPos[i], normals[i], glm::vec4(1, 0, 0, 1), glm::vec2(0, 0), 0});
                indices.push_back(i);
            }

            const std::string modelName = fmt::format("scad_part_{}", bucketIndex);
            const uint32_t modelIdx = static_cast<uint32_t>(models.size());
            models.push_back(FProcModel::CreateFromBuffers(modelName, std::move(vertices), std::move(indices), false));

            const glm::vec3 rgb(bucket.color.r, bucket.color.g, bucket.color.b);
            const float alpha = bucket.color.a;
            Material material;
            if (alpha < 0.99f)
            {
                // Translucent color() -> tinted dielectric (glass / liquid).
                material = Material::Dielectric(1.45f, 0.0f);
                material.Diffuse = glm::vec4(rgb, alpha);
            }
            else
            {
                material = Material::Lambertian(rgb);
            }
            const uint32_t materialIdx = static_cast<uint32_t>(materials.size());
            materials.push_back({material, fmt::format("scad_color_{}", bucketIndex)});

            std::array<uint32_t, 16> nodeMaterials = {0};
            nodeMaterials[0] = materialIdx;

            auto node = Node::CreateNode(
                modelName,
                glm::vec3(0.0f),
                glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
                glm::vec3(1.0f),
                static_cast<uint32_t>(nodes.size()));

            auto renderComp = std::make_shared<Runtime::RenderComponent>();
            renderComp->SetModelId(modelIdx);
            renderComp->SetVisible(true);
            renderComp->SetRayCastVisible(true);
            renderComp->SetMaterials(nodeMaterials);
            node->AddComponent(renderComp);

            nodes.push_back(node);
            ++bucketIndex;
        }

        // ---- Camera + environment ----
        cameraInit.HasSky = true;
        cameraInit.HasSun = true;
        cameraInit.SunRotation = 0.35f;
        cameraInit.SunIntensity = 500.0f;
        cameraInit.SkyIntensity = 80.0f;

        Camera defaultCam = FSceneLoader::AutoFocusCamera(cameraInit, nodes, models);
        if (cameraInit.cameras.empty())
        {
            cameraInit.cameras.push_back(defaultCam);
        }

        SPDLOG_INFO("SCAD: loaded {} -> {} color groups, {} triangles ({} warnings)",
                    filename, bucketIndex, result.triangleCount, result.warningCount);
        return true;
    }
} // namespace Assets
