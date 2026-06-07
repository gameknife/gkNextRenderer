#include "BrickPlayerLDrawShadow.hpp"

#include "Engine/Assets/Loaders/FLDrawConnectivity.h"
#include "Engine/Utilities/FileHelper.hpp"

#include <spdlog/spdlog.h>

#include <fstream>
#include <sstream>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

namespace BrickPlayer::Shadow
{
    namespace
    {
        struct FRawCylinderSection
        {
            std::string profile;
            float radius = 0.0f;
            float length = 0.0f;
        };

        struct FRawConnector
        {
            ESnapGender gender = ESnapGender::Unknown;
            std::string id;
            std::string group;
            glm::vec3 position{0.0f};
            glm::mat3 basis{1.0f};
            float radius = 0.0f;
            float length = 0.0f;
            bool slide = false;
            bool centered = false;
            std::vector<FRawCylinderSection> sections;
        };

        std::string TrimCopy(const std::string& text)
        {
            const size_t begin = text.find_first_not_of(" \t\r\n");
            if (begin == std::string::npos)
            {
                return {};
            }

            const size_t end = text.find_last_not_of(" \t\r\n");
            return text.substr(begin, end - begin + 1);
        }

        std::vector<std::string> SplitWhitespace(const std::string& text)
        {
            std::istringstream stream(text);
            std::vector<std::string> tokens;
            std::string token;
            while (stream >> token)
            {
                tokens.push_back(token);
            }
            return tokens;
        }

        bool ParseFloat(const std::string& text, float& outValue)
        {
            char* end = nullptr;
            outValue = std::strtof(text.c_str(), &end);
            return end && *end == '\0';
        }

        bool ParseInt(const std::string& text, int& outValue)
        {
            char* end = nullptr;
            outValue = static_cast<int>(std::strtol(text.c_str(), &end, 10));
            return end && *end == '\0';
        }

        std::unordered_map<std::string, std::string> ParseMetaProperties(const std::string& line)
        {
            std::unordered_map<std::string, std::string> properties;
            size_t cursor = 0;
            while (true)
            {
                const size_t open = line.find('[', cursor);
                if (open == std::string::npos)
                {
                    break;
                }

                const size_t close = line.find(']', open + 1);
                if (close == std::string::npos)
                {
                    break;
                }

                const std::string content = TrimCopy(line.substr(open + 1, close - open - 1));
                const size_t equalPos = content.find('=');
                if (equalPos != std::string::npos)
                {
                    std::string key = ToLowerCopy(TrimCopy(content.substr(0, equalPos)));
                    std::string value = TrimCopy(content.substr(equalPos + 1));
                    properties[key] = value;
                }

                cursor = close + 1;
            }

            return properties;
        }

        bool ParseLDrawReference(const std::string& line, glm::mat4& outTransform, std::string& outSubfile)
        {
            const std::string trimmed = TrimCopy(line);
            if (trimmed.empty() || trimmed[0] != '1')
            {
                return false;
            }

            std::istringstream iss(trimmed);
            int type = 0;
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

            iss >> type >> color >> x >> y >> z >> a >> b >> c >> d >> e >> f >> g >> h >> i;
            if (!iss || type != 1)
            {
                return false;
            }

            std::getline(iss, outSubfile);
            outSubfile = TrimCopy(outSubfile);
            if (outSubfile.empty())
            {
                return false;
            }

            outTransform = glm::mat4(
                a, d, g, 0.0f,
                b, e, h, 0.0f,
                c, f, i, 0.0f,
                x, y, z, 1.0f);
            return true;
        }

        glm::mat3 ParseOrientationMatrix(const std::string& value)
        {
            const std::vector<std::string> tokens = SplitWhitespace(value);
            if (tokens.size() != 9)
            {
                return glm::mat3(1.0f);
            }

            float v[9] = {};
            for (size_t index = 0; index < 9; ++index)
            {
                if (!ParseFloat(tokens[index], v[index]))
                {
                    return glm::mat3(1.0f);
                }
            }

            return glm::mat3(
                v[0], v[3], v[6],
                v[1], v[4], v[7],
                v[2], v[5], v[8]);
        }

        glm::mat4 BuildMetaTransform(const std::unordered_map<std::string, std::string>& properties)
        {
            glm::mat4 transform(1.0f);

            auto oriIt = properties.find("ori");
            if (oriIt != properties.end())
            {
                const glm::mat3 basis = ParseOrientationMatrix(oriIt->second);
                transform[0] = glm::vec4(basis[0], 0.0f);
                transform[1] = glm::vec4(basis[1], 0.0f);
                transform[2] = glm::vec4(basis[2], 0.0f);
            }

            auto posIt = properties.find("pos");
            if (posIt != properties.end())
            {
                const std::vector<std::string> tokens = SplitWhitespace(posIt->second);
                if (tokens.size() == 3)
                {
                    float x = 0.0f;
                    float y = 0.0f;
                    float z = 0.0f;
                    if (ParseFloat(tokens[0], x) && ParseFloat(tokens[1], y) && ParseFloat(tokens[2], z))
                    {
                        transform[3] = glm::vec4(x, y, z, 1.0f);
                    }
                }
            }

            return transform;
        }

        std::vector<float> BuildAxisOffsets(int count, float step, bool centered)
        {
            std::vector<float> offsets;
            if (count <= 0)
            {
                offsets.push_back(0.0f);
                return offsets;
            }

            offsets.reserve(static_cast<size_t>(count));
            if (centered)
            {
                const float half = static_cast<float>(count - 1) * 0.5f;
                for (int index = 0; index < count; ++index)
                {
                    offsets.push_back((static_cast<float>(index) - half) * step);
                }
            }
            else
            {
                for (int index = 0; index < count; ++index)
                {
                    offsets.push_back(static_cast<float>(index) * step);
                }
            }

            return offsets;
        }

        std::vector<glm::vec3> BuildGridOffsets(const std::unordered_map<std::string, std::string>& properties)
        {
            auto gridIt = properties.find("grid");
            if (gridIt == properties.end())
            {
                return {glm::vec3(0.0f)};
            }

            const std::vector<std::string> tokens = SplitWhitespace(gridIt->second);

            bool centerX = false;
            bool centerZ = false;
            int countX = 1;
            int countZ = 1;
            float stepX = 0.0f;
            float stepZ = 0.0f;

            if (tokens.size() == 6)
            {
                centerX = ToLowerCopy(tokens[0]) == "c";
                centerZ = ToLowerCopy(tokens[2]) == "c";
                if (!ParseInt(tokens[1], countX) || !ParseInt(tokens[3], countZ)
                    || !ParseFloat(tokens[4], stepX) || !ParseFloat(tokens[5], stepZ))
                {
                    return {glm::vec3(0.0f)};
                }
            }
            else if (tokens.size() == 5)
            {
                centerX = ToLowerCopy(tokens[0]) == "c";
                if (!ParseInt(tokens[1], countX) || !ParseInt(tokens[2], countZ)
                    || !ParseFloat(tokens[3], stepX) || !ParseFloat(tokens[4], stepZ))
                {
                    return {glm::vec3(0.0f)};
                }
            }
            else
            {
                return {glm::vec3(0.0f)};
            }

            std::vector<glm::vec3> offsets;
            const std::vector<float> xOffsets = BuildAxisOffsets(countX, stepX, centerX);
            const std::vector<float> zOffsets = BuildAxisOffsets(countZ, stepZ, centerZ);
            offsets.reserve(xOffsets.size() * zOffsets.size());
            for (float xOffset : xOffsets)
            {
                for (float zOffset : zOffsets)
                {
                    offsets.emplace_back(xOffset, 0.0f, zOffset);
                }
            }

            return offsets;
        }

        ESnapGender ParseGender(const std::unordered_map<std::string, std::string>& properties)
        {
            auto it = properties.find("gender");
            if (it == properties.end())
            {
                return ESnapGender::Unknown;
            }

            const std::string gender = ToLowerCopy(it->second);
            if (gender == "m")
            {
                return ESnapGender::Male;
            }
            if (gender == "f")
            {
                return ESnapGender::Female;
            }
            return ESnapGender::Unknown;
        }

        std::vector<FRawCylinderSection> ParseCylinderSections(const std::string& value)
        {
            std::vector<FRawCylinderSection> sections;
            const std::vector<std::string> tokens = SplitWhitespace(value);
            for (size_t index = 0; index + 2 < tokens.size(); index += 3)
            {
                FRawCylinderSection section;
                section.profile = tokens[index];
                if (!ParseFloat(tokens[index + 1], section.radius) || !ParseFloat(tokens[index + 2], section.length))
                {
                    sections.clear();
                    return sections;
                }
                sections.push_back(std::move(section));
            }

            return sections;
        }

        float ComputeCylinderRadius(const std::vector<FRawCylinderSection>& sections)
        {
            float radius = 0.0f;
            for (const FRawCylinderSection& section : sections)
            {
                radius = std::max(radius, section.radius);
            }
            return radius;
        }

        float ComputeCylinderLength(const std::vector<FRawCylinderSection>& sections)
        {
            float length = 0.0f;
            for (const FRawCylinderSection& section : sections)
            {
                length += section.length;
            }
            return length;
        }

        std::vector<FRawConnector> ParseCylinderConnectors(const std::unordered_map<std::string, std::string>& properties)
        {
            const ESnapGender gender = ParseGender(properties);
            auto secsIt = properties.find("secs");
            if (gender == ESnapGender::Unknown || secsIt == properties.end())
            {
                return {};
            }

            const std::vector<FRawCylinderSection> sections = ParseCylinderSections(secsIt->second);
            if (sections.empty())
            {
                return {};
            }

            const glm::mat4 metaTransform = BuildMetaTransform(properties);
            const glm::mat3 baseBasis(metaTransform);
            const glm::vec3 basePosition(metaTransform[3]);
            const std::vector<glm::vec3> gridOffsets = BuildGridOffsets(properties);

            std::vector<FRawConnector> connectors;
            connectors.reserve(gridOffsets.size());

            const float radius = ComputeCylinderRadius(sections);
            const float length = ComputeCylinderLength(sections);
            const bool slide = ToLowerCopy(properties.contains("slide") ? properties.at("slide") : "") == "true";
            const bool centered = ToLowerCopy(properties.contains("center") ? properties.at("center") : "") == "true";
            const std::string id = properties.contains("id") ? properties.at("id") : "";
            const std::string group = properties.contains("group") ? properties.at("group") : "";

            for (const glm::vec3& gridOffset : gridOffsets)
            {
                FRawConnector connector;
                connector.gender = gender;
                connector.id = id;
                connector.group = group;
                connector.position = basePosition + baseBasis * gridOffset;
                connector.basis = baseBasis;
                connector.radius = radius;
                connector.length = length;
                connector.slide = slide;
                connector.centered = centered;
                connector.sections = sections;
                connectors.push_back(std::move(connector));
            }

            return connectors;
        }

        glm::mat3 OrthonormalizeBasis(const glm::mat3& basis)
        {
            glm::vec3 xAxis = basis[0];
            glm::vec3 yAxis = basis[1];

            if (glm::dot(xAxis, xAxis) < 1e-6f)
            {
                xAxis = glm::vec3(1.0f, 0.0f, 0.0f);
            }
            if (glm::dot(yAxis, yAxis) < 1e-6f)
            {
                yAxis = glm::vec3(0.0f, 1.0f, 0.0f);
            }

            xAxis = glm::normalize(xAxis);
            yAxis = glm::normalize(yAxis - xAxis * glm::dot(xAxis, yAxis));
            if (glm::dot(yAxis, yAxis) < 1e-6f)
            {
                yAxis = glm::vec3(0.0f, 1.0f, 0.0f);
            }

            glm::vec3 zAxis = glm::cross(xAxis, yAxis);
            if (glm::dot(zAxis, zAxis) < 1e-6f)
            {
                zAxis = glm::vec3(0.0f, 0.0f, 1.0f);
            }
            zAxis = glm::normalize(zAxis);
            xAxis = glm::normalize(glm::cross(yAxis, zAxis));

            glm::mat3 result(1.0f);
            result[0] = xAxis;
            result[1] = yAxis;
            result[2] = zAxis;
            return result;
        }

        FRawConnector ApplyTransformToConnector(const FRawConnector& connector, const glm::mat4& transform)
        {
            FRawConnector transformed = connector;
            transformed.position = glm::vec3(transform * glm::vec4(connector.position, 1.0f));

            const glm::mat3 linear(transform);
            const glm::vec3 scaledX = linear * connector.basis[0];
            const glm::vec3 scaledY = linear * connector.basis[1];
            const glm::vec3 scaledZ = linear * connector.basis[2];

            const float radiusScale = 0.5f * (glm::length(scaledX) + glm::length(scaledZ));
            const float lengthScale = glm::length(scaledY);

            glm::mat3 transformedBasis(1.0f);
            transformedBasis[0] = scaledX;
            transformedBasis[1] = scaledY;
            transformedBasis[2] = scaledZ;
            transformed.basis = OrthonormalizeBasis(transformedBasis);
            transformed.radius *= radiusScale;
            transformed.length *= lengthScale;
            for (FRawCylinderSection& section : transformed.sections)
            {
                section.radius *= radiusScale;
                section.length *= lengthScale;
            }

            return transformed;
        }

        glm::vec3 ConvertPointToEngine(const glm::vec3& ldrawPoint, float lduToWorldScale)
        {
            return glm::vec3(-ldrawPoint.x, -ldrawPoint.y, ldrawPoint.z) * lduToWorldScale;
        }

        glm::mat3 ConvertBasisToEngine(const glm::mat3& ldrawBasis)
        {
            glm::mat3 flip(1.0f);
            flip[0][0] = -1.0f;
            flip[1][1] = -1.0f;
            return OrthonormalizeBasis(flip * ldrawBasis * flip);
        }
    }

    bool FShadowLibrary::Initialize(float lduToWorldScale)
    {
        if (initialized_ && glm::abs(lduToWorldScale_ - lduToWorldScale) < 1e-6f)
        {
            return true;
        }

        if (!Assets::EnsureLDrawLibraryPakMounted())
        {
            SPDLOG_WARN("BrickPlayer: LDraw pak '{}' is not available", Assets::kLDrawLibraryPakPath);
            return false;
        }

        if (!originalResolver_.BuildIndexFromMountedRoot(Assets::kLDrawLibraryRootEntry)
            || !shadowResolver_.BuildIndexFromMountedRoot(Assets::kLDrawShadowLibraryRootEntry))
        {
            SPDLOG_WARN("BrickPlayer: failed to index LDraw library roots from pak");
            return false;
        }

        shadowResolver_.SetWarnOnMissing(false);
        lduToWorldScale_ = lduToWorldScale;
        connectorCache_.clear();

        // Check if Studio .conn files are available in the pak
        auto* pakSystem = Utilities::Package::FPackageFileSystem::TryGetInstance();
        connAvailable_ = pakSystem && pakSystem->HasMountedEntry(
            std::string(Assets::kLDrawConnectivityRootEntry) + "/3001.conn");
        if (connAvailable_)
        {
            SPDLOG_INFO("BrickPlayer: Studio .conn connectivity data available");
        }

        initialized_ = true;
        return true;
    }

    const std::vector<FSnapConnector>& FShadowLibrary::GetConnectorsForPart(const std::string& partFile) const
    {
        static const std::vector<FSnapConnector> emptyConnectors;

        if (!initialized_ || partFile.empty())
        {
            return emptyConnectors;
        }

        const std::string cacheKey = ToLowerCopy(partFile);
        auto it = connectorCache_.find(cacheKey);
        if (it != connectorCache_.end())
        {
            return it->second;
        }

        auto [insertedIt, inserted] = connectorCache_.emplace(cacheKey, LoadConnectorsForPart(partFile));
        return insertedIt->second;
    }

    std::vector<FSnapConnector> FShadowLibrary::LoadConnectorsForPart(const std::string& partFile) const
    {
        // Try Studio .conn file first (broader coverage)
        if (connAvailable_)
        {
            std::vector<FSnapConnector> connResult = LoadConnectorsFromConnFile(partFile);
            if (!connResult.empty())
            {
                return connResult;
            }
        }

        // Fall back to shadow library (text-based LDCAD SNAP_CYL)
        return LoadConnectorsFromShadow(partFile);
    }

    std::vector<FSnapConnector> FShadowLibrary::LoadConnectorsFromConnFile(const std::string& partFile) const
    {
        // Extract part number from filename: "parts/3001.dat" -> "3001"
        std::string partNumber = partFile;
        const size_t lastSlash = partNumber.find_last_of("/\\");
        if (lastSlash != std::string::npos)
        {
            partNumber = partNumber.substr(lastSlash + 1);
        }
        const size_t dotPos = partNumber.rfind('.');
        if (dotPos != std::string::npos)
        {
            partNumber = partNumber.substr(0, dotPos);
        }
        partNumber = ToLowerCopy(partNumber);

        const std::string connEntry = std::string(Assets::kLDrawConnectivityRootEntry)
            + "/" + partNumber + ".conn";

        Assets::LDrawConn::FConnFile connFile;
        if (!Assets::LDrawConn::LoadConnFile(connEntry, connFile))
        {
            return {};
        }

        std::vector<FSnapConnector> connectors;

        // Convert grid elements: extract stud/anti-stud center positions
        for (const auto& grid : connFile.grids)
        {
            // Only process top stud (3,23) and bottom anti-stud (2,0)/(2,1) grids
            const bool isTopStud = (grid.id1 == 3 && grid.id2 == 23);
            const bool isBotAntiStud = (grid.id1 == 2 && (grid.id2 == 0 || grid.id2 == 1));
            if (!isTopStud && !isBotAntiStud)
            {
                continue;
            }

            const ESnapGender gender = isTopStud ? ESnapGender::Male : ESnapGender::Female;

            // Grid spacing: 10 LDU per half-stud unit (full stud pitch = 20 LDU)
            constexpr float kHalfStudPitch = 10.0f;
            constexpr float kStudRadius = 3.0f;   // LDU
            constexpr float kStudLength = 4.0f;    // LDU

            // Iterate vertices; stud centers have type=10 or type=20, value=4
            for (uint16_t row = 0; row <= grid.gridZ; ++row)
            {
                for (uint16_t col = 0; col <= grid.gridX; ++col)
                {
                    const size_t idx = static_cast<size_t>(row) * (grid.gridX + 1) + col;
                    const auto& vertex = grid.vertices[idx];

                    if (!vertex.IsStudCenter())
                    {
                        continue;
                    }

                    // Compute LDU position relative to grid origin
                    const float localX = static_cast<float>(col) * kHalfStudPitch;
                    const float localZ = -static_cast<float>(row) * kHalfStudPitch;
                    const glm::vec3 gridOffset(localX, 0.0f, localZ);

                    // Transform to part-local LDU coordinates
                    const glm::vec3 lduPos = grid.transform.position
                        + grid.transform.rotation * gridOffset;

                    // Convert from LDraw coordinate system to engine
                    // LDraw: X-right, Y-down, Z-back -> Engine: X-left, Y-up, Z-back
                    const glm::vec3 enginePos = ConvertPointToEngine(lduPos, lduToWorldScale_);
                    const glm::mat3 engineBasis = ConvertBasisToEngine(grid.transform.rotation);

                    FSnapConnector connector;
                    connector.kind = ESnapKind::Cylinder;
                    connector.gender = gender;
                    connector.localPosition = enginePos;
                    connector.localRotation = glm::quat_cast(engineBasis);
                    connector.radius = kStudRadius * lduToWorldScale_;
                    connector.length = kStudLength * lduToWorldScale_;
                    connector.sections.push_back({"R", connector.radius, connector.length});
                    connectors.push_back(std::move(connector));
                }
            }
        }

        // Convert point connectors (Technic pin/axle/clip/bar)
        for (const auto& point : connFile.points)
        {
            ESnapGender gender = ESnapGender::Unknown;
            float radius = 0.0f;
            std::string profile = "R";  // Round by default

            switch (point.subType)
            {
            case 3:   // Technic Pin (male)
                gender = ESnapGender::Male;
                radius = 3.0f;
                break;
            case 2:   // Technic Pinhole (female)
                gender = ESnapGender::Female;
                radius = 3.0f;
                break;
            case 7:   // Technic Axle (male)
                gender = ESnapGender::Male;
                radius = 3.0f;
                profile = "A";
                break;
            case 6:   // Technic Axlehole (female)
                gender = ESnapGender::Female;
                radius = 3.0f;
                profile = "A";
                break;
            case 13:  // Bar (male)
                gender = ESnapGender::Male;
                radius = 1.6f;
                break;
            case 11:  // Bar for Round Hole (male)
                gender = ESnapGender::Male;
                radius = 1.6f;
                break;
            case 12:  // Clip (female)
                gender = ESnapGender::Female;
                radius = 2.0f;
                break;
            case 10:  // Round Hole (female)
                gender = ESnapGender::Female;
                radius = 1.6f;
                break;
            default:
                continue;  // Unknown sub-type, skip
            }

            const glm::vec3 enginePos = ConvertPointToEngine(point.transform.position, lduToWorldScale_);
            const glm::mat3 engineBasis = ConvertBasisToEngine(point.transform.rotation);

            FSnapConnector connector;
            connector.kind = ESnapKind::Cylinder;
            connector.gender = gender;
            connector.localPosition = enginePos;
            connector.localRotation = glm::quat_cast(engineBasis);
            connector.radius = radius * lduToWorldScale_;
            connector.length = point.length * lduToWorldScale_;
            connector.sections.push_back({profile, connector.radius, connector.length});
            connectors.push_back(std::move(connector));
        }

        return connectors;
    }

    std::vector<FSnapConnector> FShadowLibrary::LoadConnectorsFromShadow(const std::string& partFile) const
    {
        std::unordered_map<std::string, std::vector<FRawConnector>> originalCache;
        std::unordered_map<std::string, std::vector<FRawConnector>> shadowOnlyCache;
        std::unordered_set<std::string> originalStack;
        std::unordered_set<std::string> shadowStack;

        std::function<std::vector<FRawConnector>(const std::string&, std::vector<FRawConnector>)> applyShadowPatch;
        std::function<std::vector<FRawConnector>(const std::string&)> loadShadowOnly;
        std::function<std::vector<FRawConnector>(const std::string&)> collectOriginal;

        applyShadowPatch = [&](const std::string& logicalRef, std::vector<FRawConnector> connectors) -> std::vector<FRawConnector>
        {
            const std::string resolvedShadow = shadowResolver_.Resolve(logicalRef);
            if (resolvedShadow.empty())
            {
                return connectors;
            }

            std::string shadowContent;
            if (!Assets::LoadLDrawTextResource(resolvedShadow, shadowContent))
            {
                return connectors;
            }

            std::istringstream shadowStream(shadowContent);
            std::string line;
            while (std::getline(shadowStream, line))
            {
                const std::string trimmed = TrimCopy(line);
                if (trimmed.rfind("0 !LDCAD ", 0) != 0)
                {
                    continue;
                }

                const size_t commandStart = std::string("0 !LDCAD ").size();
                size_t commandEnd = trimmed.find(' ', commandStart);
                if (commandEnd == std::string::npos)
                {
                    commandEnd = trimmed.find('[', commandStart);
                }
                const std::string command = trimmed.substr(commandStart, commandEnd - commandStart);
                const std::unordered_map<std::string, std::string> properties = ParseMetaProperties(trimmed);

                if (command == "SNAP_CLEAR")
                {
                    connectors.clear();
                    continue;
                }

                if (command == "SNAP_INCL")
                {
                    auto refIt = properties.find("ref");
                    if (refIt == properties.end() || refIt->second.empty())
                    {
                        continue;
                    }

                    const glm::mat4 includeTransform = BuildMetaTransform(properties);
                    const std::vector<FRawConnector> included = loadShadowOnly(refIt->second);
                    for (const FRawConnector& includedConnector : included)
                    {
                        connectors.push_back(ApplyTransformToConnector(includedConnector, includeTransform));
                    }
                    continue;
                }

                if (command == "SNAP_CYL")
                {
                    std::vector<FRawConnector> cylinders = ParseCylinderConnectors(properties);
                    connectors.insert(connectors.end(), cylinders.begin(), cylinders.end());
                }
            }

            return connectors;
        };

        loadShadowOnly = [&](const std::string& logicalRef) -> std::vector<FRawConnector>
        {
            const std::string cacheKey = ToLowerCopy(logicalRef);
            auto cacheIt = shadowOnlyCache.find(cacheKey);
            if (cacheIt != shadowOnlyCache.end())
            {
                return cacheIt->second;
            }

            if (shadowStack.count(cacheKey))
            {
                return {};
            }

            shadowStack.insert(cacheKey);
            std::vector<FRawConnector> connectors = applyShadowPatch(logicalRef, {});
            shadowStack.erase(cacheKey);
            shadowOnlyCache[cacheKey] = connectors;
            return connectors;
        };

        collectOriginal = [&](const std::string& logicalRef) -> std::vector<FRawConnector>
        {
            const std::string cacheKey = ToLowerCopy(logicalRef);
            auto cacheIt = originalCache.find(cacheKey);
            if (cacheIt != originalCache.end())
            {
                return cacheIt->second;
            }

            if (originalStack.count(cacheKey))
            {
                return {};
            }

            originalStack.insert(cacheKey);
            std::vector<FRawConnector> connectors;

            std::string resolvedOriginal = originalResolver_.Resolve(logicalRef);
            if (resolvedOriginal.empty())
            {
                resolvedOriginal = logicalRef;
            }

            if (!resolvedOriginal.empty())
            {
                std::string originalContent;
                if (Assets::LoadLDrawTextResource(resolvedOriginal, originalContent))
                {
                    std::istringstream originalStream(originalContent);
                    std::string line;
                    while (std::getline(originalStream, line))
                    {
                        glm::mat4 referenceTransform(1.0f);
                        std::string subfile;
                        if (!ParseLDrawReference(line, referenceTransform, subfile))
                        {
                            continue;
                        }

                        const std::vector<FRawConnector> childConnectors = collectOriginal(subfile);
                        for (const FRawConnector& childConnector : childConnectors)
                        {
                            connectors.push_back(ApplyTransformToConnector(childConnector, referenceTransform));
                        }
                    }
                }
            }

            connectors = applyShadowPatch(logicalRef, std::move(connectors));

            originalStack.erase(cacheKey);
            originalCache[cacheKey] = connectors;
            return connectors;
        };

        const std::vector<FRawConnector> rawConnectors = collectOriginal(partFile);

        std::vector<FSnapConnector> connectors;
        connectors.reserve(rawConnectors.size());
        for (const FRawConnector& rawConnector : rawConnectors)
        {
            FSnapConnector connector;
            connector.kind = ESnapKind::Cylinder;
            connector.gender = rawConnector.gender;
            connector.id = rawConnector.id;
            connector.group = rawConnector.group;
            connector.localPosition = ConvertPointToEngine(rawConnector.position, lduToWorldScale_);
            connector.localRotation = glm::quat_cast(ConvertBasisToEngine(rawConnector.basis));
            connector.radius = rawConnector.radius * lduToWorldScale_;
            connector.length = rawConnector.length * lduToWorldScale_;
            connector.slide = rawConnector.slide;
            connector.centered = rawConnector.centered;
            connector.sections.reserve(rawConnector.sections.size());
            for (const FRawCylinderSection& rawSection : rawConnector.sections)
            {
                connector.sections.push_back({
                    rawSection.profile,
                    rawSection.radius * lduToWorldScale_,
                    rawSection.length * lduToWorldScale_
                });
            }
            connectors.push_back(std::move(connector));
        }

        return connectors;
    }
}
