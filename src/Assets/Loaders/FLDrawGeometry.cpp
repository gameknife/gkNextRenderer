#include "Assets/Loaders/FLDrawGeometry.h"

#include <array>
#include <cmath>
#include <compare>
#include <map>

namespace Assets
{
    namespace
    {
        constexpr float kNormalPositionEpsilon = 1e-6f;
        constexpr float kSmoothNormalAngleDegrees = 45.0f;
        constexpr float kFaceNormalEpsilon = 1e-12f;

        struct QuantizedPosition
        {
            int64_t x;
            int64_t y;
            int64_t z;

            auto operator<=>(const QuantizedPosition&) const = default;
        };

        struct FaceBuildData
        {
            glm::vec3 positions[3];
            glm::vec3 unitNormal;
            glm::vec3 weightedNormal;
            uint32_t materialIndex;
        };

        QuantizedPosition QuantizePosition(const glm::vec3& position)
        {
            return {
                static_cast<int64_t>(std::llround(position.x / kNormalPositionEpsilon)),
                static_cast<int64_t>(std::llround(position.y / kNormalPositionEpsilon)),
                static_cast<int64_t>(std::llround(position.z / kNormalPositionEpsilon))
            };
        }

        glm::vec3 ConvertPosition(const glm::vec3& position, float lduToWorldScale)
        {
            glm::vec3 converted = position;
            converted.x = -converted.x;
            converted.y = -converted.y;
            converted *= lduToWorldScale;
            return converted;
        }

    }

    LDrawBuiltGeometry FLDrawGeometry::BuildPartGeometry(const LDrawPartTemplate& tmpl,
                                                         const LDrawLoadOptions& options)
    {
        const float lduToWorldScale = SanitizeLDrawLduToWorldScale(options.lduToWorldScale);

        std::map<int, std::vector<const LDrawFace*>> facesByColor;
        for (const auto& face : tmpl.faces)
            facesByColor[face.colorCode].push_back(&face);

        LDrawBuiltGeometry geometry;
        for (const auto& [colorCode, _] : facesByColor)
            geometry.sectionColors.push_back(colorCode);

        std::vector<FaceBuildData> builtFaces;
        std::vector<std::array<QuantizedPosition, 3>> quantizedCorners;
        std::map<QuantizedPosition, std::vector<size_t>> positionToFaces;

        uint32_t sectionIdx = 0;
        for (int colorCode : geometry.sectionColors)
        {
            const auto& colorFaces = facesByColor[colorCode];
            for (const LDrawFace* face : colorFaces)
            {
                FaceBuildData faceData{};
                faceData.materialIndex = sectionIdx;

                for (int vertexIdx = 0; vertexIdx < 3; ++vertexIdx)
                    faceData.positions[vertexIdx] = ConvertPosition(face->vertices[vertexIdx], lduToWorldScale);

                faceData.weightedNormal = glm::cross(
                    faceData.positions[1] - faceData.positions[0],
                    faceData.positions[2] - faceData.positions[0]);

                const float normalLengthSq = glm::dot(faceData.weightedNormal, faceData.weightedNormal);
                faceData.unitNormal = normalLengthSq <= kFaceNormalEpsilon
                    ? glm::vec3(0.0f, 1.0f, 0.0f)
                    : glm::normalize(faceData.weightedNormal);

                const size_t faceIndex = builtFaces.size();
                builtFaces.push_back(faceData);

                std::array<QuantizedPosition, 3> cornerKeys{};
                for (int vertexIdx = 0; vertexIdx < 3; ++vertexIdx)
                {
                    cornerKeys[vertexIdx] = QuantizePosition(faceData.positions[vertexIdx]);
                    positionToFaces[cornerKeys[vertexIdx]].push_back(faceIndex);
                }
                quantizedCorners.push_back(cornerKeys);
            }
            sectionIdx++;
        }

        geometry.vertices.reserve(builtFaces.size() * 3);
        geometry.indices.reserve(builtFaces.size() * 3);

        const float smoothDotThreshold = std::cos(glm::radians(kSmoothNormalAngleDegrees));
        for (size_t faceIndex = 0; faceIndex < builtFaces.size(); ++faceIndex)
        {
            const FaceBuildData& face = builtFaces[faceIndex];
            const uint32_t baseIndex = static_cast<uint32_t>(geometry.vertices.size());

            for (int vertexIdx = 0; vertexIdx < 3; ++vertexIdx)
            {
                glm::vec3 smoothedNormal(0.0f);
                const auto& sharedFaces = positionToFaces[quantizedCorners[faceIndex][vertexIdx]];
                for (size_t sharedFaceIndex : sharedFaces)
                {
                    const FaceBuildData& sharedFace = builtFaces[sharedFaceIndex];
                    if (glm::dot(face.unitNormal, sharedFace.unitNormal) >= smoothDotThreshold)
                        smoothedNormal += sharedFace.weightedNormal;
                }

                if (glm::dot(smoothedNormal, smoothedNormal) <= kFaceNormalEpsilon)
                    smoothedNormal = face.unitNormal;
                else
                    smoothedNormal = glm::normalize(smoothedNormal);

                Vertex vertex{};
                vertex.Position = face.positions[vertexIdx];
                vertex.Normal = smoothedNormal;
                vertex.Tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
                vertex.TexCoord = glm::vec2(0.0f);
                vertex.MaterialIndex = face.materialIndex;
                geometry.vertices.push_back(vertex);
            }

            geometry.indices.push_back(baseIndex);
            geometry.indices.push_back(baseIndex + 1);
            geometry.indices.push_back(baseIndex + 2);
        }

        return geometry;
    }
}
