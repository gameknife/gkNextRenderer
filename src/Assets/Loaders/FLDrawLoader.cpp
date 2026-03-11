#include "Assets/Loaders/FLDrawLoader.h"
#include "Assets/Loaders/FLDrawGeometry.h"
#include "Assets/Loaders/FLDrawParser.h"
#include "Assets/Loaders/FProcModel.h"
#include "Assets/Loaders/FSceneLoader.h"
#include "Assets/Data/Material.hpp"
#include "Assets/Core/Node.h"
#include "Runtime/Components/RenderComponent.h"
#include "Utilities/FileHelper.hpp"

#include <spdlog/spdlog.h>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <functional>
#include <unordered_set>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>

namespace Assets
{
    static std::string ToLowerStr(const std::string& s)
    {
        std::string result = s;
        std::transform(result.begin(), result.end(), result.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return result;
    }

    namespace
    {
        struct LDrawNodePlacement
        {
            int colorCode = 16;
            glm::mat4 transform = glm::mat4(1.0f);
            std::string partFile;
            std::string partKey;
            bool isHierarchicalSubmodel = false;
            std::vector<LDrawNodePlacement> children;
        };

        struct SceneBounds
        {
            glm::vec3 min = glm::vec3(FLT_MAX);
            glm::vec3 max = glm::vec3(-FLT_MAX);
            bool valid = false;
        };

        glm::vec3 LightenTowardsWhite(const glm::vec3& color, float colorRetention)
        {
            return glm::mix(glm::vec3(1.0f), color, glm::clamp(colorRetention, 0.0f, 1.0f));
        }

        glm::vec3 BlendColors(const glm::vec3& baseColor, const glm::vec3& detailColor, float factor)
        {
            return glm::mix(baseColor, detailColor, glm::clamp(factor, 0.0f, 1.0f));
        }

        glm::vec3 ChromeColor(const glm::vec3& color)
        {
            return glm::mix(color, glm::vec3(1.0f), 0.25f);
        }

        bool ParseType1Reference(
            const std::string& line,
            int parentColor,
            LDrawNodePlacement& placement)
        {
            std::string trimmed = line;
            size_t start = trimmed.find_first_not_of(" \t\r\n");
            if (start == std::string::npos)
                return false;

            trimmed = trimmed.substr(start);
            if (trimmed.empty() || trimmed[0] != '1')
                return false;

            std::istringstream iss(trimmed);
            int type = 0;
            iss >> type;
            if (type != 1)
                return false;

            int color = 16;
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
            float a = 1.0f;
            float b = 0.0f;
            float c = 0.0f;
            float d = 0.0f;
            float e = 1.0f;
            float f = 0.0f;
            float g = 0.0f;
            float h = 0.0f;
            float i = 1.0f;
            std::string subfile;

            iss >> color >> x >> y >> z >> a >> b >> c >> d >> e >> f >> g >> h >> i;
            std::getline(iss, subfile);

            size_t subfileStart = subfile.find_first_not_of(" \t");
            if (subfileStart == std::string::npos)
                return false;

            subfile = subfile.substr(subfileStart);
            size_t subfileEnd = subfile.find_last_not_of(" \t\r\n");
            if (subfileEnd != std::string::npos)
                subfile = subfile.substr(0, subfileEnd + 1);

            if (subfile.empty())
                return false;

            placement.colorCode = (color == 16) ? parentColor : color;
            placement.transform = glm::mat4(
                a, d, g, 0.0f,
                b, e, h, 0.0f,
                c, f, i, 0.0f,
                x, y, z, 1.0f);
            placement.partFile = subfile;
            placement.partKey = ToLowerStr(subfile);
            return true;
        }

        bool IsHierarchicalMPDSubmodel(
            const std::string& partFile,
            const std::unordered_map<std::string, std::string>& mpdSubfiles)
        {
            std::string key = ToLowerStr(partFile);
            if (mpdSubfiles.count(key) == 0)
                return false;

            std::string ext = ToLowerStr(std::filesystem::path(partFile).extension().string());
            return ext != ".dat";
        }

        std::vector<LDrawNodePlacement> ParsePlacementTree(
            const std::string& content,
            int parentColor,
            const std::unordered_map<std::string, std::string>& mpdSubfiles,
            std::unordered_set<std::string>& activeSubmodels)
        {
            std::vector<LDrawNodePlacement> placements;
            std::istringstream stream(content);
            std::string line;

            while (std::getline(stream, line))
            {
                LDrawNodePlacement placement;
                if (!ParseType1Reference(line, parentColor, placement))
                    continue;

                placement.isHierarchicalSubmodel =
                    IsHierarchicalMPDSubmodel(placement.partFile, mpdSubfiles);

                if (placement.isHierarchicalSubmodel)
                {
                    if (activeSubmodels.count(placement.partKey) > 0)
                    {
                        SPDLOG_WARN("LDraw: detected recursive MPD submodel '{}', skipping nested expansion", placement.partFile);
                    }
                    else
                    {
                        auto mpdIt = mpdSubfiles.find(placement.partKey);
                        if (mpdIt != mpdSubfiles.end())
                        {
                            activeSubmodels.insert(placement.partKey);
                            placement.children = ParsePlacementTree(
                                mpdIt->second,
                                placement.colorCode,
                                mpdSubfiles,
                                activeSubmodels);
                            activeSubmodels.erase(placement.partKey);
                        }
                    }
                }

                placements.push_back(std::move(placement));
            }

            return placements;
        }

        glm::mat4 BuildNodeTransform(const glm::mat4& ldrawMat, float lduToWorldScale)
        {
            glm::mat4 tfm = ldrawMat;
            tfm[0][2] = -ldrawMat[0][2];
            tfm[1][2] = -ldrawMat[1][2];
            tfm[2][0] = -ldrawMat[2][0];
            tfm[2][1] = -ldrawMat[2][1];
            tfm[3][0] = -ldrawMat[3][0] * lduToWorldScale;
            tfm[3][1] = -ldrawMat[3][1] * lduToWorldScale;
            tfm[3][2] = ldrawMat[3][2] * lduToWorldScale;
            tfm[3][3] = 1.0f;
            return tfm;
        }

        std::string BuildNodeName(const std::string& partFile, uint32_t serial)
        {
            std::string baseName = std::filesystem::path(partFile).filename().string();
            if (baseName.empty())
                baseName = partFile;
            return baseName + "_" + std::to_string(serial);
        }

        Material CreateMixtureMaterial(const glm::vec3& diffuse, float roughness, float metalness, float ior)
        {
            Material material = Material::Mixture(diffuse, roughness);
            material.Metalness = metalness;
            material.RefractionIndex = ior;
            material.RefractionIndex2 = ior;
            return material;
        }

        Material CreateMetallicMaterial(const glm::vec3& diffuse, float roughness, float ior)
        {
            Material material = Material::Metallic(diffuse, roughness);
            material.RefractionIndex = ior;
            material.RefractionIndex2 = ior;
            return material;
        }

        Material CreateDielectricMaterial(const glm::vec3& diffuse, float alpha, float roughness, float ior)
        {
            Material material = Material::Dielectric(ior, roughness);
            material.Diffuse = glm::vec4(diffuse, alpha);
            material.RefractionIndex = ior;
            material.RefractionIndex2 = ior;
            return material;
        }

        Material BuildMaterialFromLDrawColor(const LDrawColor& color)
        {
            const bool isTransparent = color.alpha < 0.99f;

            switch (color.finish)
            {
            case LDrawColor::Finish::Chrome:
                return CreateMetallicMaterial(ChromeColor(color.diffuse), 0.0f, 2.4f);

            case LDrawColor::Finish::Pearlescent:
                return CreateMixtureMaterial(LightenTowardsWhite(color.diffuse, 0.9f), 0.2f, 0.5f, 1.6f);

            case LDrawColor::Finish::MatteMetallic:
                return CreateMixtureMaterial(color.diffuse, 0.2f, 0.8f, 1.45f);

            case LDrawColor::Finish::Rubber:
                if (isTransparent)
                    return CreateDielectricMaterial(color.diffuse, color.alpha, 0.4f, 1.45f);
                return CreateMixtureMaterial(color.diffuse, 0.4f, 0.0f, 1.45f);

            case LDrawColor::Finish::Glitter:
            {
                glm::vec3 glitterColor = color.hasSecondaryDiffuse
                    ? LightenTowardsWhite(color.secondaryDiffuse, 0.5f)
                    : LightenTowardsWhite(color.diffuse, 0.5f);
                glm::vec3 blendedColor = BlendColors(color.diffuse, glitterColor, 0.05f);
                if (isTransparent)
                    return CreateDielectricMaterial(blendedColor, color.alpha, 0.2f, 1.585f);
                return CreateMixtureMaterial(blendedColor, 0.2f, 0.05f, 1.585f);
            }

            case LDrawColor::Finish::Speckle:
            {
                glm::vec3 speckleColor = color.hasSecondaryDiffuse
                    ? LightenTowardsWhite(color.secondaryDiffuse, 0.5f)
                    : LightenTowardsWhite(color.diffuse, 0.5f);
                glm::vec3 blendedColor = BlendColors(color.diffuse, speckleColor, 0.05f);
                return CreateMixtureMaterial(blendedColor, 0.025f, 0.05f, 1.45f);
            }

            case LDrawColor::Finish::Solid:
            default:
                if (isTransparent)
                    return CreateDielectricMaterial(color.diffuse, color.alpha, 0.05f, 1.585f);
                return CreateMixtureMaterial(color.diffuse, 0.025f, 0.0f, 1.45f);
            }
        }

        SceneBounds CalculateSceneBounds(
            const std::vector<std::shared_ptr<Node>>& nodes,
            const std::vector<Model>& models)
        {
            SceneBounds bounds;

            for (const auto& node : nodes)
            {
                auto render = node->GetComponent<Runtime::RenderComponent>();
                if (!render || !render->IsDrawable())
                    continue;

                uint32_t modelIdx = render->GetModelId();
                if (modelIdx >= models.size())
                    continue;

                const auto& model = models[modelIdx];
                glm::vec3 aabbMin = model.GetLocalAABBMin();
                glm::vec3 aabbMax = model.GetLocalAABBMax();

                glm::vec3 corners[8] = {
                    {aabbMin.x, aabbMin.y, aabbMin.z},
                    {aabbMax.x, aabbMin.y, aabbMin.z},
                    {aabbMin.x, aabbMax.y, aabbMin.z},
                    {aabbMax.x, aabbMax.y, aabbMin.z},
                    {aabbMin.x, aabbMin.y, aabbMax.z},
                    {aabbMax.x, aabbMin.y, aabbMax.z},
                    {aabbMin.x, aabbMax.y, aabbMax.z},
                    {aabbMax.x, aabbMax.y, aabbMax.z}
                };

                const glm::mat4& worldTransform = node->WorldTransform();
                for (const glm::vec3& corner : corners)
                {
                    glm::vec3 worldPos = glm::vec3(worldTransform * glm::vec4(corner, 1.0f));
                    bounds.min = glm::min(bounds.min, worldPos);
                    bounds.max = glm::max(bounds.max, worldPos);
                    bounds.valid = true;
                }
            }

            return bounds;
        }

        bool AppendLDrawFloor(
            const SceneBounds& sceneBounds,
            std::vector<std::shared_ptr<Node>>& nodes,
            std::vector<Model>& models,
            std::vector<FMaterial>& materials)
        {
            if (!sceneBounds.valid)
                return false;

            const glm::vec3 extent = glm::max(sceneBounds.max - sceneBounds.min, glm::vec3(0.001f));
            const glm::vec3 center = (sceneBounds.min + sceneBounds.max) * 0.5f;
            const float longestExtent = std::max(extent.x, std::max(extent.y, extent.z));
            const float halfSizeX = glm::max(extent.x * 1.5f, 2.0f);
            const float halfSizeZ = glm::max(extent.z * 1.5f, 2.0f);
            const float thickness = glm::max(longestExtent * 0.1f, 0.5f);
            const float floorTop = sceneBounds.min.y;

            const glm::vec3 floorMin(
                center.x - halfSizeX,
                floorTop - thickness,
                center.z - halfSizeZ);
            const glm::vec3 floorMax(
                center.x + halfSizeX,
                floorTop,
                center.z + halfSizeZ);

            const uint32_t floorModelIdx = static_cast<uint32_t>(models.size());
            models.push_back(FProcModel::CreateBox(floorMin, floorMax));

            Material floorMaterial = CreateMixtureMaterial(glm::vec3(0.97f, 0.97f, 0.97f), 0.2f, 0.0f, 1.45f);
            const uint32_t floorMaterialIdx = static_cast<uint32_t>(materials.size());
            materials.push_back({floorMaterial, "LDraw Floor"});

            std::array<uint32_t, 16> floorMaterials = {0};
            floorMaterials[0] = floorMaterialIdx;

            auto floorNode = Node::CreateNode(
                "ldraw_floor",
                glm::vec3(0.0f),
                glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
                glm::vec3(1.0f),
                static_cast<uint32_t>(nodes.size()));

            auto renderComp = std::make_shared<Runtime::RenderComponent>();
            renderComp->SetModelId(floorModelIdx);
            renderComp->SetVisible(true);
            renderComp->SetRayCastVisible(false);
            renderComp->SetMaterial(floorMaterials);
            floorNode->AddComponent(renderComp);

            nodes.push_back(floorNode);
            return true;
        }

        PartModelInfo ResolvePartModel(
            const std::string& partFile,
            bool directGeometryOnly,
            LDrawParser& parser,
            LDrawFileResolver& fileResolver,
            std::unordered_map<std::string, PartModelInfo>& fullPartModels,
            std::unordered_map<std::string, PartModelInfo>& directPartModels,
            const std::function<PartModelInfo(const LDrawPartTemplate&, std::vector<Model>&)>& buildPartModel,
            std::vector<Model>& models)
        {
            std::string partKey = ToLowerStr(partFile);
            auto& modelCache = directGeometryOnly ? directPartModels : fullPartModels;

            auto modelIt = modelCache.find(partKey);
            if (modelIt != modelCache.end())
                return modelIt->second;

            LDrawPartTemplate tmpl;
            if (parser.HasMPDSubfile(partKey))
            {
                tmpl = directGeometryOnly
                    ? parser.ParseFileDirectGeometry(partFile)
                    : parser.ParseFile(partFile);
            }
            else
            {
                std::string resolvedPath = fileResolver.Resolve(partFile);
                if (!resolvedPath.empty())
                {
                    tmpl = directGeometryOnly
                        ? parser.ParseFileDirectGeometry(resolvedPath)
                        : parser.ParseFile(resolvedPath);
                }
            }

            PartModelInfo info {};
            info.modelIdx = UINT32_MAX;
            if (!tmpl.faces.empty())
                info = buildPartModel(tmpl, models);

            modelCache[partKey] = info;
            return info;
        }

        void ApplyPartMaterials(
            const PartModelInfo& partInfo,
            int inheritedColorCode,
            uint32_t defaultMatIdx,
            const std::unordered_map<int, uint32_t>& colorToMatIdx,
            std::array<uint32_t, 16>& matArray)
        {
            matArray.fill(0);
            for (size_t sectionIdx = 0; sectionIdx < partInfo.sectionColors.size() && sectionIdx < matArray.size(); ++sectionIdx)
            {
                int colorCode = partInfo.sectionColors[sectionIdx];
                if (colorCode == kLDrawColorInherit)
                    colorCode = inheritedColorCode;

                auto colorIt = colorToMatIdx.find(colorCode);
                matArray[sectionIdx] = (colorIt != colorToMatIdx.end()) ? colorIt->second : defaultMatIdx;
            }
        }

        void InstantiatePlacementTree(
            const LDrawNodePlacement& placement,
            std::shared_ptr<Node> parent,
            uint32_t& nodeSerial,
            const LDrawLoadOptions& options,
            LDrawParser& parser,
            LDrawFileResolver& fileResolver,
            uint32_t defaultMatIdx,
            const std::unordered_map<int, uint32_t>& colorToMatIdx,
            std::unordered_map<std::string, PartModelInfo>& fullPartModels,
            std::unordered_map<std::string, PartModelInfo>& directPartModels,
            const std::function<PartModelInfo(const LDrawPartTemplate&, std::vector<Model>&)>& buildPartModel,
            std::vector<std::shared_ptr<Node>>& nodes,
            std::vector<Model>& models)
        {
            glm::vec3 scale;
            glm::vec3 translation;
            glm::vec3 skew;
            glm::quat rotation;
            glm::vec4 perspective;
            glm::decompose(
                BuildNodeTransform(placement.transform, options.lduToWorldScale),
                scale,
                rotation,
                translation,
                skew,
                perspective);

            auto node = Node::CreateNode(
                BuildNodeName(placement.partFile, nodeSerial++),
                translation,
                rotation,
                scale,
                static_cast<uint32_t>(nodes.size()));

            nodes.push_back(node);
            if (parent)
                node->SetParent(parent);

            const PartModelInfo partInfo = ResolvePartModel(
                placement.partFile,
                placement.isHierarchicalSubmodel,
                parser,
                fileResolver,
                fullPartModels,
                directPartModels,
                buildPartModel,
                models);

            if (partInfo.modelIdx != UINT32_MAX)
            {
                std::array<uint32_t, 16> matArray = {0};
                ApplyPartMaterials(partInfo, placement.colorCode, defaultMatIdx, colorToMatIdx, matArray);

                auto renderComp = std::make_shared<Runtime::RenderComponent>();
                renderComp->SetModelId(partInfo.modelIdx);
                renderComp->SetVisible(true);
                renderComp->SetMaterial(matArray);
                node->AddComponent(renderComp);
            }

            for (const auto& child : placement.children)
            {
                InstantiatePlacementTree(
                    child,
                    node,
                    nodeSerial,
                    options,
                    parser,
                    fileResolver,
                    defaultMatIdx,
                    colorToMatIdx,
                    fullPartModels,
                    directPartModels,
                    buildPartModel,
                    nodes,
                    models);
            }
        }
    }

    PartModelInfo FLDrawLoader::BuildPartModel(
        const LDrawPartTemplate& tmpl,
        std::vector<Model>& models,
        const LDrawLoadOptions& options)
    {
        LDrawBuiltGeometry geometry = FLDrawGeometry::BuildPartGeometry(tmpl, options);

        PartModelInfo info;
        info.sectionColors = std::move(geometry.sectionColors);

        if (!geometry.vertices.empty())
        {
            Model model(tmpl.filename, std::move(geometry.vertices), std::move(geometry.indices), false);
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
        std::vector<Skeleton>& skeletons,
        const LDrawLoadOptions& options)
    {
        LDrawLoadOptions normalizedOptions = options;
        if (!std::isfinite(normalizedOptions.lduToWorldScale) || normalizedOptions.lduToWorldScale <= 0.0f)
        {
            SPDLOG_WARN("LDraw: invalid LDU scale {}, falling back to {}",
                        normalizedOptions.lduToWorldScale,
                        defaultLDrawLduToWorldScale);
            normalizedOptions.lduToWorldScale = defaultLDrawLduToWorldScale;
        }

        const size_t initialModelCount = models.size();
        const size_t initialMaterialCount = materials.size();
        const size_t initialNodeCount = nodes.size();

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
            Material mat = BuildMaterialFromLDrawColor(color);

            uint32_t idx = static_cast<uint32_t>(materials.size());
            materials.push_back({mat, color.name});
            colorToMatIdx[code] = idx;
        }

        // Default material for unresolved colors
        uint32_t defaultMatIdx = matBase > 0 ? matBase - 1 : 0;
        auto blackIt = colorToMatIdx.find(0);
        if (blackIt != colorToMatIdx.end())
            defaultMatIdx = blackIt->second;

        // Parse the .ldr/.mpd file
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

        // Read entire file content
        std::string fileContent((std::istreambuf_iterator<char>(ldrFile)),
                                 std::istreambuf_iterator<char>());
        ldrFile.close();

        std::string mainModelContent;
        std::unordered_map<std::string, std::string> mpdSubfiles;
        {
            std::istringstream contentStream(fileContent);
            std::string line;
            std::string currentFileName;
            std::string currentContent;
            bool isMPD = false;

            while (std::getline(contentStream, line))
            {
                std::string trimmed = line;
                size_t start = trimmed.find_first_not_of(" \t\r\n");
                if (start != std::string::npos)
                    trimmed = trimmed.substr(start);

                // Check for "0 FILE <name>" directive
                if (trimmed.find("0 FILE ") == 0)
                {
                    // Save previous sub-file if any
                    if (!currentFileName.empty())
                    {
                        std::string key = ToLowerStr(currentFileName);
                        if (mainModelContent.empty())
                            mainModelContent = currentContent;
                        mpdSubfiles[key] = currentContent;
                    }

                    // Extract new sub-file name
                    currentFileName = trimmed.substr(7); // after "0 FILE "
                    size_t ep = currentFileName.find_last_not_of(" \t\r\n");
                    if (ep != std::string::npos)
                        currentFileName = currentFileName.substr(0, ep + 1);
                    currentContent.clear();
                    isMPD = true;
                    continue;
                }

                // Skip "0 NOFILE" lines
                if (trimmed == "0 NOFILE")
                    continue;

                currentContent += line + "\n";
            }

            // Save last sub-file
            if (!currentFileName.empty() && !currentContent.empty())
            {
                std::string key = ToLowerStr(currentFileName);
                if (mainModelContent.empty())
                    mainModelContent = currentContent;
                mpdSubfiles[key] = currentContent;
            }

            if (isMPD)
            {
                SPDLOG_INFO("LDraw: MPD file with {} embedded sub-files", mpdSubfiles.size());
                parser.SetMPDSubfiles(mpdSubfiles);
            }
            else
            {
                // Not an MPD - use entire file as main model
                mainModelContent = fileContent;
            }
        }

        std::unordered_set<std::string> activeSubmodels;
        std::vector<LDrawNodePlacement> placements =
            ParsePlacementTree(mainModelContent, 16, mpdSubfiles, activeSubmodels);

        SPDLOG_INFO("LDraw: found {} part placements in scene", placements.size());

        std::unordered_map<std::string, PartModelInfo> fullPartModels;
        std::unordered_map<std::string, PartModelInfo> directPartModels;
        uint32_t nodeSerial = 0;
        auto buildPartModel = [&normalizedOptions](const LDrawPartTemplate& tmpl, std::vector<Model>& outputModels)
        {
            return FLDrawLoader::BuildPartModel(tmpl, outputModels, normalizedOptions);
        };

        for (const auto& placement : placements)
        {
            InstantiatePlacementTree(
                placement,
                nullptr,
                nodeSerial,
                normalizedOptions,
                parser,
                fileResolver,
                defaultMatIdx,
                colorToMatIdx,
                fullPartModels,
                directPartModels,
                buildPartModel,
                nodes,
                models);
        }

        // Clean up MPD sub-files from parser
        parser.ClearMPDSubfiles();

        // Set up camera
        cameraInit.HasSky = true;
        cameraInit.HasSun = false;
        cameraInit.SunRotation = 0.5f;
        cameraInit.SunIntensity = 500.0f;
        cameraInit.SkyIntensity = 100.0f;

        Camera defaultCam = FSceneLoader::AutoFocusCamera(cameraInit, nodes, models);
        AppendLDrawFloor(CalculateSceneBounds(nodes, models), nodes, models, materials);

        SPDLOG_INFO("LDraw: created {} models, {} materials, {} nodes",
                     models.size() - initialModelCount,
                     materials.size() - initialMaterialCount,
                     nodes.size() - initialNodeCount);

        if (cameraInit.cameras.empty())
        {
            cameraInit.cameras.push_back(defaultCam);
        }

        return true;
    }
}
