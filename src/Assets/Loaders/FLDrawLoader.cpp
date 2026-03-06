#include "Assets/Loaders/FLDrawLoader.h"
#include "Assets/Loaders/FLDrawParser.h"
#include "Assets/Loaders/FSceneLoader.h"
#include "Assets/Data/Material.hpp"
#include "Assets/Core/Node.h"
#include "Runtime/Components/RenderComponent.h"
#include "Utilities/FileHelper.hpp"

#include <spdlog/spdlog.h>
#include <sstream>
#include <algorithm>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>

namespace Assets
{
    constexpr float kLDrawScale = 0.001f;

    static std::string ToLowerStr(const std::string& s)
    {
        std::string result = s;
        std::transform(result.begin(), result.end(), result.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return result;
    }

    PartModelInfo FLDrawLoader::BuildPartModel(
        const LDrawPartTemplate& tmpl,
        std::vector<Model>& models)
    {
        // Group faces by color code
        // Deterministic ordering: kLDrawColorInherit first (if present), then sorted color codes
        std::map<int, std::vector<const LDrawFace*>> facesByColor;
        for (const auto& face : tmpl.faces)
            facesByColor[face.colorCode].push_back(&face);

        std::vector<int> sectionColors;
        // kLDrawColorInherit (-1) sorts first in std::map, so it's already first
        for (const auto& [colorCode, _] : facesByColor)
            sectionColors.push_back(colorCode);

        // Build vertex/index arrays
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;

        uint32_t sectionIdx = 0;
        for (int colorCode : sectionColors)
        {
            const auto& colorFaces = facesByColor[colorCode];
            for (const LDrawFace* face : colorFaces)
            {
                glm::vec3 v0 = face->vertices[0];
                glm::vec3 v1 = face->vertices[1];
                glm::vec3 v2 = face->vertices[2];

                // Y-inversion (LDraw Y-down -> engine Y-up) + scale
                v0.y = -v0.y;
                v1.y = -v1.y;
                v2.y = -v2.y;
                v0 *= kLDrawScale;
                v1 *= kLDrawScale;
                v2 *= kLDrawScale;

                // Swap winding to compensate Y-inversion
                std::swap(v1, v2);

                // Flat normal
                glm::vec3 normal = glm::normalize(glm::cross(v1 - v0, v2 - v0));
                if (glm::any(glm::isnan(normal)))
                    normal = glm::vec3(0, 1, 0);

                uint32_t baseIdx = static_cast<uint32_t>(vertices.size());

                auto makeVert = [&](const glm::vec3& pos) -> Vertex
                {
                    Vertex v{};
                    v.Position = pos;
                    v.Normal = normal;
                    v.Tangent = glm::vec4(1, 0, 0, 1);
                    v.TexCoord = glm::vec2(0);
                    v.MaterialIndex = sectionIdx;
                    return v;
                };

                vertices.push_back(makeVert(v0));
                vertices.push_back(makeVert(v1));
                vertices.push_back(makeVert(v2));

                indices.push_back(baseIdx);
                indices.push_back(baseIdx + 1);
                indices.push_back(baseIdx + 2);
            }
            sectionIdx++;
        }

        PartModelInfo info;
        info.sectionColors = std::move(sectionColors);

        if (!vertices.empty())
        {
            Model model(tmpl.filename, std::move(vertices), std::move(indices), false);
            info.modelIdx = static_cast<uint32_t>(models.size());
            models.push_back(std::move(model));
        }
        else
        {
            info.modelIdx = UINT32_MAX;
        }

        return info;
    }

    bool FLDrawLoader::LoadLDrawScene(
        const std::string& filename,
        EnvironmentSetting& cameraInit,
        std::vector<std::shared_ptr<Node>>& nodes,
        std::vector<Model>& models,
        std::vector<FMaterial>& materials,
        std::vector<LightObject>& lights,
        std::vector<AnimationTrack>& tracks,
        std::vector<Skeleton>& skeletons)
    {
        // Resolve paths
        std::filesystem::path ldrawRoot = std::filesystem::path("..") / "assets" / "ldraw";
        ldrawRoot = ldrawRoot.lexically_normal();
        std::string ldrawRootStr = ldrawRoot.string() + "/";
        std::string ldconfigPath = (ldrawRoot / "LDConfig.ldr").string();

        // Initialize LDraw subsystem (static lazy init)
        static LDrawColorTable colorTable;
        static LDrawFileResolver fileResolver;
        static bool initialized = false;
        if (!initialized)
        {
            colorTable.Parse(ldconfigPath);
            fileResolver.BuildIndex(ldrawRootStr);
            initialized = true;
        }

        // Create materials from color table
        std::unordered_map<int, uint32_t> colorToMatIdx;
        uint32_t matBase = static_cast<uint32_t>(materials.size());

        for (const auto& [code, color] : colorTable.AllColors())
        {
            Material mat;
            if (color.finish == LDrawColor::Finish::Chrome)
            {
                mat = Material::Metallic(color.diffuse, 0.01f);
            }
            else if (color.finish == LDrawColor::Finish::Pearlescent || color.finish == LDrawColor::Finish::MatteMetallic)
            {
                mat = Material::Metallic(color.diffuse, 0.15f);
            }
            else if (color.alpha < 0.99f)
            {
                mat = Material::Dielectric(1.5f, 0.01f);
                mat.Diffuse = glm::vec4(color.diffuse, color.alpha);
            }
            else
            {
                mat = Material::Lambertian(color.diffuse);
            }

            uint32_t idx = static_cast<uint32_t>(materials.size());
            materials.push_back({mat, color.name});
            colorToMatIdx[code] = idx;
        }

        // Default material for unresolved colors
        uint32_t defaultMatIdx = matBase > 0 ? matBase - 1 : 0;
        auto blackIt = colorToMatIdx.find(0);
        if (blackIt != colorToMatIdx.end())
            defaultMatIdx = blackIt->second;

        // Parse the .ldr file
        LDrawParser parser(colorTable, fileResolver);

        std::string ldrPath;
        std::filesystem::path fpath(filename);
        if (fpath.is_absolute())
            ldrPath = filename;
        else
            ldrPath = (std::filesystem::path("..") / fpath).string();

        std::ifstream ldrFile(ldrPath);
        if (!ldrFile.is_open())
        {
            SPDLOG_ERROR("LDraw: cannot open scene file '{}'", ldrPath);
            return false;
        }

        // Parse top-level type-1 references
        struct PartPlacement
        {
            int colorCode;
            glm::mat4 transform;
            std::string partFile;
        };
        std::vector<PartPlacement> placements;

        std::string line;
        while (std::getline(ldrFile, line))
        {
            size_t start = line.find_first_not_of(" \t\r\n");
            if (start == std::string::npos)
                continue;
            line = line.substr(start);
            if (line.empty() || line[0] != '1')
                continue;

            std::istringstream iss(line);
            int type;
            iss >> type;
            if (type != 1)
                continue;

            int color;
            float x, y, z, a, b, c, d, e, f, g, h, i;
            std::string subfile;

            iss >> color >> x >> y >> z >> a >> b >> c >> d >> e >> f >> g >> h >> i;
            std::getline(iss, subfile);
            {
                size_t sp = subfile.find_first_not_of(" \t");
                if (sp != std::string::npos)
                    subfile = subfile.substr(sp);
                size_t ep = subfile.find_last_not_of(" \t\r\n");
                if (ep != std::string::npos)
                    subfile = subfile.substr(0, ep + 1);
            }

            if (subfile.empty())
                continue;

            PartPlacement placement;
            placement.colorCode = color;
            placement.transform = glm::mat4(
                a, d, g, 0.0f,
                b, e, h, 0.0f,
                c, f, i, 0.0f,
                x, y, z, 1.0f
            );
            placement.partFile = subfile;
            placements.push_back(placement);
        }

        SPDLOG_INFO("LDraw: found {} part placements in scene", placements.size());

        // Build models for each unique part, storing section info
        std::unordered_map<std::string, PartModelInfo> partModels;

        for (const auto& placement : placements)
        {
            std::string partKey = ToLowerStr(placement.partFile);
            if (partModels.count(partKey))
                continue;

            std::string resolvedPath = fileResolver.Resolve(placement.partFile);
            if (resolvedPath.empty())
                continue;

            LDrawPartTemplate tmpl = parser.ParseFile(resolvedPath);
            if (tmpl.faces.empty())
                continue;

            partModels[partKey] = BuildPartModel(tmpl, models);
        }

        // Create nodes for each placement
        for (size_t pi = 0; pi < placements.size(); ++pi)
        {
            const auto& placement = placements[pi];
            std::string partKey = ToLowerStr(placement.partFile);

            auto partIt = partModels.find(partKey);
            if (partIt == partModels.end() || partIt->second.modelIdx == UINT32_MAX)
                continue;

            const auto& partInfo = partIt->second;

            // Build Y-flip conjugated transform: F * T_ldraw * F
            // F = diag(1, -1, 1, 1)
            // (F*R*F)[c][r] = F[r]*F[c]*R[c][r]
            glm::mat4 ldrawMat = placement.transform;
            glm::mat4 tfm = ldrawMat;
            tfm[0][1] = -ldrawMat[0][1];
            tfm[1][0] = -ldrawMat[1][0];
            tfm[1][2] = -ldrawMat[1][2];
            tfm[2][1] = -ldrawMat[2][1];
            tfm[3][0] =  ldrawMat[3][0] * kLDrawScale;
            tfm[3][1] = -ldrawMat[3][1] * kLDrawScale;
            tfm[3][2] =  ldrawMat[3][2] * kLDrawScale;
            tfm[3][3] = 1.0f;

            // Decompose into TRS
            glm::vec3 scale, trans, skew;
            glm::quat rotation;
            glm::vec4 perspective;
            glm::decompose(tfm, scale, rotation, trans, skew, perspective);

            std::string nodeName = partKey + "_" + std::to_string(pi);
            auto node = Node::CreateNode(nodeName, trans, rotation, scale,
                                        static_cast<uint32_t>(nodes.size()));

            // Set up material array matching section colors
            std::array<uint32_t, 16> matArray = {0};
            for (size_t s = 0; s < partInfo.sectionColors.size() && s < 16; ++s)
            {
                int colorCode = partInfo.sectionColors[s];
                if (colorCode == kLDrawColorInherit)
                {
                    auto cmIt = colorToMatIdx.find(placement.colorCode);
                    matArray[s] = (cmIt != colorToMatIdx.end()) ? cmIt->second : defaultMatIdx;
                }
                else
                {
                    auto cmIt = colorToMatIdx.find(colorCode);
                    matArray[s] = (cmIt != colorToMatIdx.end()) ? cmIt->second : defaultMatIdx;
                }
            }

            auto renderComp = std::make_shared<Runtime::RenderComponent>();
            renderComp->SetModelId(partInfo.modelIdx);
            renderComp->SetVisible(true);
            renderComp->SetMaterial(matArray);
            node->AddComponent(renderComp);

            nodes.push_back(node);
        }

        SPDLOG_INFO("LDraw: created {} models, {} materials, {} nodes",
                     partModels.size(), colorToMatIdx.size(), nodes.size());

        // Set up camera
        cameraInit.HasSky = true;
        cameraInit.HasSun = true;
        cameraInit.SunRotation = 0.5f;
        cameraInit.SunIntensity = 500.0f;
        cameraInit.SkyIntensity = 100.0f;

        Camera defaultCam = FSceneLoader::AutoFocusCamera(cameraInit, nodes, models);
        if (cameraInit.cameras.empty())
        {
            cameraInit.cameras.push_back(defaultCam);
        }

        return true;
    }
}
