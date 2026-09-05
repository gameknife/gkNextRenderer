#include "Engine/Assets/Core/Model.hpp"
#include "Engine/Utilities/FileHelper.hpp"
#include "Engine/Assets/Loaders/LoaderUtils.hpp"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/hash.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>

#include <chrono>
#include <filesystem>
#include <fmt/format.h>

#include <meshoptimizer.h>
#include <xxhash.h>

#include "Engine/Runtime/Engine.hpp"

#define PROVOKING_VERTEX 1

using namespace glm;

namespace std
{
    template <>
    struct hash<Assets::Vertex> final
    {
        size_t operator()(Assets::Vertex const& vertex) const noexcept
        {
            return
                Combine(hash<vec3>()(vertex.Position),
                        Combine(hash<vec3>()(vertex.Normal),
                                Combine(hash<vec2>()(vertex.TexCoord),
                                        hash<int>()(vertex.MaterialIndex))));
        }

    private:
        static size_t Combine(size_t hash0, size_t hash1)
        {
            return hash0 ^ (hash1 + 0x9e3779b9 + (hash0 << 6) + (hash0 >> 2));
        }
    };
}

namespace Assets
{
    CascadeShadowSetup EnvironmentSetting::ComputeSunCascades(
        const glm::mat4& cameraViewProj,
        float cameraNear,
        float cameraFar,
        float shadowFar) const
    {
        constexpr int CASCADE_COUNT = 4;
        constexpr float SPLIT_LAMBDA = 0.75f;   // Bias toward logarithmic splits.
        constexpr float SHADOW_MAP_RESOLUTION = 1024.0f;

        const float n = std::max(cameraNear, 1e-3f);
        const float f = std::max(std::min(shadowFar, cameraFar), n * 2.0f);

        float splits[CASCADE_COUNT + 1];
        splits[0] = n;
        for (int i = 1; i <= CASCADE_COUNT; ++i)
        {
            const float p = float(i) / float(CASCADE_COUNT);
            const float logS = n * std::pow(f / n, p);
            const float uniS = n + (f - n) * p;
            splits[i] = SPLIT_LAMBDA * logS + (1.0f - SPLIT_LAMBDA) * uniS;
        }

        const glm::vec3 sunDir = SunDirection();
        const glm::vec3 lightDir = glm::normalize(-sunDir);
        const glm::mat4 invCamVP = glm::inverse(cameraViewProj);

        // Eight main-camera frustum corners. Reverse-Z puts the Vulkan near plane at z=1 and
        // the far plane at z=0; keep the first four corners as near and the last four as far.
        const glm::vec4 ndc[8] = {
            {-1, -1, 1, 1}, {1, -1, 1, 1}, {1, 1, 1, 1}, {-1, 1, 1, 1},
            {-1, -1, 0, 1}, {1, -1, 0, 1}, {1, 1, 0, 1}, {-1, 1, 0, 1},
        };
        glm::vec3 worldFull[8];
        for (int i = 0; i < 8; ++i)
        {
            glm::vec4 w = invCamVP * ndc[i];
            worldFull[i] = glm::vec3(w) / w.w;
        }

        CascadeShadowSetup result{};
        for (int c = 0; c < CASCADE_COUNT; ++c)
        {
            const float zNear = splits[c];
            const float zFar = splits[c + 1];
            const float denom = std::max(cameraFar - cameraNear, 1e-3f);
            const float tN = (zNear - cameraNear) / denom;
            const float tF = (zFar - cameraNear) / denom;

            glm::vec3 corners[8];
            for (int i = 0; i < 4; ++i)
            {
                const glm::vec3 ray = worldFull[i + 4] - worldFull[i];
                corners[i] = worldFull[i] + ray * tN;
                corners[i + 4] = worldFull[i] + ray * tF;
            }

            glm::vec3 center(0.0f);
            for (int i = 0; i < 8; ++i) center += corners[i];
            center /= 8.0f;

            float radius = 0.0f;
            for (int i = 0; i < 8; ++i)
                radius = std::max(radius, glm::length(corners[i] - center));
            radius = std::ceil(radius * 16.0f) / 16.0f;

            const glm::vec3 lightUp = std::abs(lightDir.y) > 0.99f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
            const float backOff = radius + 50.0f;       // Move the light camera back to include rear occluders.
            const glm::vec3 lightPos = center - lightDir * backOff;
            glm::mat4 lightView = glm::lookAt(lightPos, center, lightUp);

            // Stable CSM: snap the cascade center to light-space texels to prevent camera-motion shimmer.
            const float worldUnitsPerTexel = (radius * 2.0f) / SHADOW_MAP_RESOLUTION;
            glm::vec4 centerLS = lightView * glm::vec4(center, 1.0f);
            centerLS.x = std::floor(centerLS.x / worldUnitsPerTexel) * worldUnitsPerTexel;
            centerLS.y = std::floor(centerLS.y / worldUnitsPerTexel) * worldUnitsPerTexel;
            const glm::vec3 snappedCenter = glm::vec3(glm::inverse(lightView) * centerLS);
            lightView = glm::lookAt(snappedCenter - lightDir * backOff, snappedCenter, lightUp);

            const glm::mat4 lightProj = glm::ortho(-radius, radius, -radius, radius, 0.0f, 2.0f * backOff);

            result.viewProjection[c] = lightProj * lightView;
            result.splits[c] = zFar;
        }

        return result;
    }

    template <typename T>
    T AnimationChannel<T>::Sample(float time) const
    {
        if (Keys.size() == 1)
        {
            return Keys[0].Value;
        }
        for ( int i = 0; i < Keys.size() - 1; i++ )
        {
            auto& key = Keys[i];
            auto& keyNext = Keys[i + 1];
            if (time >= key.Time && time < keyNext.Time)
            {
                const float interval = keyNext.Time - key.Time;
                if (interval <= 1.0e-8f)
                {
                    return keyNext.Value;
                }
                float t = (time - key.Time) / interval;
                return glm::mix(key.Value, keyNext.Value, t);
            }

            if ( i == 0 && time < key.Time )
            {
                return key.Value;
            }

            if ( i == Keys.size() - 2)
            {
                return keyNext.Value;
            }
        }
        return T{};
    }

    // Partial specialization for T == glm::quat.
    template <>
    glm::quat AnimationChannel<glm::quat>::Sample(float time) const
    {
        if (Keys.size() == 1)
        {
            return Keys[0].Value;
        }
        for ( int i = 0; i < Keys.size() - 1; i++ )
        {
            auto& key = Keys[i];
            auto& keyNext = Keys[i + 1];
            if (time >= key.Time && time < keyNext.Time)
            {
                const float interval = keyNext.Time - key.Time;
                if (interval <= 1.0e-8f)
                {
                    return keyNext.Value;
                }
                float t = (time - key.Time) / interval;
                return glm::slerp(key.Value, keyNext.Value, t);
            }

            if ( i == 0 && time < key.Time )
            {
                return key.Value;
            }

            if ( i == Keys.size() - 2)
            {
                return keyNext.Value;
            }
        }
        return {};
    }

    template glm::vec3 AnimationChannel<glm::vec3>::Sample(float time) const;
    template float AnimationChannel<float>::Sample(float time) const;

    void AnimationTrack::Sample(float time, glm::vec3& translation, glm::quat& rotation, glm::vec3& scaling)
    {
        if (!TranslationChannel.Keys.empty())
        {
            translation = TranslationChannel.Sample(time);
        }
        if (!RotationChannel.Keys.empty())
        {
            rotation = RotationChannel.Sample(time);
        }
        if (!ScaleChannel.Keys.empty())
        {
            scaling = ScaleChannel.Sample(time);
        }
    }

    void AnimationTrack::Sample(float time, EnvironmentSetting& environment)
    {
        if (!SunRotationChannel.Keys.empty())
        {
            environment.SunRotation = SunRotationChannel.Sample(time);
        }
        if (!SunElevationChannel.Keys.empty())
        {
            environment.SunElevation = SunElevationChannel.Sample(time);
        }
        if (!SkyRotationChannel.Keys.empty())
        {
            environment.SkyRotation = SkyRotationChannel.Sample(time);
        }
        if (!SunIntensityChannel.Keys.empty())
        {
            environment.SunIntensity = SunIntensityChannel.Sample(time);
        }
        if (!SkyIntensityChannel.Keys.empty())
        {
            environment.SkyIntensity = SkyIntensityChannel.Sample(time);
        }
        if (!SunColorChannel.Keys.empty())
        {
            environment.SunColor = SunColorChannel.Sample(time);
        }
        if (!SkyColorChannel.Keys.empty())
        {
            environment.SkyColor = SkyColorChannel.Sample(time);
        }
    }

    void Model::BuildLods()
    {
        if (!lodIndices_.empty() || indices_.empty() || vertices_.empty())
        {
            return;
        }

        // Skinned meshes are excluded: the synthesised-vertex path would have to extend the
        // parallel weight/joint arrays too, and Scene::Reload silently drops a model's skin data
        // when those arrays disagree with the vertex count. Characters are also not what the
        // vertex-bound scenes are made of.
        if (!weights_.empty())
        {
            return;
        }

        const size_t indexCount = indices_.size();
        // Below this a level costs more in table/draw bookkeeping than it saves in vertex work.
        constexpr size_t minTrianglesForLod = 32;
        if (indexCount < minTrianglesForLod * 3)
        {
            return;
        }

        // meshopt_simplify collapses edges, and an edge only exists between vertices that are the
        // same vertex. A flat-shaded mesh gives every triangle its own three vertices, so nothing
        // is ever adjacent and simplification silently returns the input unchanged. Welding on
        // position alone rebuilds the adjacency without touching the real vertex buffer.
        std::vector<uint32_t> remap(vertices_.size());
        const meshopt_Stream positionStream{&vertices_[0].Position, sizeof(glm::vec3), sizeof(Vertex)};
        const size_t weldedCount = meshopt_generateVertexRemapMulti(
            remap.data(), indices_.data(), indexCount, vertices_.size(), &positionStream, 1);
        if (weldedCount == 0)
        {
            return;
        }

        std::vector<uint32_t> weldedIndices(indexCount);
        meshopt_remapIndexBuffer(weldedIndices.data(), indices_.data(), indexCount, remap.data());
        std::vector<glm::vec3> weldedPositions(weldedCount);
        std::vector<uint32_t> firstOriginal(weldedCount, ~0u);
        for (uint32_t original = 0; original < vertices_.size(); ++original)
        {
            const uint32_t welded = remap[original];
            weldedPositions[welded] = vertices_[original].Position;
            if (firstOriginal[welded] == ~0u)
            {
                firstOriginal[welded] = original;
            }
        }

        // Welding that collapses most of the buffer means the seams were attribute seams, i.e. the
        // mesh is flat shaded. Mapping a simplified corner back to some original vertex would then
        // hand it a normal belonging to a face that no longer exists -- measured at 39-55 degrees
        // of error on the faceted asteroids, with outright flips at the coarsest level. Those
        // levels get freshly synthesised vertices carrying the recomputed face normal instead.
        // A smooth mesh keeps sharing its original vertices, which costs no extra memory.
        const bool synthesizeVertices = weldedCount * 2 < vertices_.size();

        size_t previousIndexCount = indexCount;
        for (uint32_t level = 1; level < static_cast<uint32_t>(MAX_MODEL_LOD_COUNT); ++level)
        {
            const float ratio = std::pow(0.5f, static_cast<float>(level));
            const size_t targetIndexCount = static_cast<size_t>(static_cast<double>(indexCount) * ratio) / 3 * 3;
            std::vector<uint32_t> simplified(indexCount);
            const size_t resultCount = meshopt_simplify(
                simplified.data(), weldedIndices.data(), indexCount, &weldedPositions[0].x, weldedCount,
                sizeof(glm::vec3), targetIndexCount, 1.0f, 0, nullptr);

            // Simplification stops short when topology runs out. A level that saved nothing over
            // the previous one would only add a table entry and a pop, so end the chain there.
            if (resultCount < 3 || resultCount * 4 > previousIndexCount * 3)
            {
                break;
            }
            simplified.resize(resultCount);

            if (synthesizeVertices)
            {
                std::vector<uint32_t> emitted(resultCount);
                for (size_t triangle = 0; triangle + 2 < resultCount; triangle += 3)
                {
                    const glm::vec3& a = weldedPositions[simplified[triangle]];
                    const glm::vec3& b = weldedPositions[simplified[triangle + 1]];
                    const glm::vec3& c = weldedPositions[simplified[triangle + 2]];
                    const glm::vec3 edge0 = b - a;
                    const glm::vec3 edge1 = c - a;
                    const glm::vec3 crossProduct = glm::cross(edge0, edge1);
                    const float crossLength = glm::length(crossProduct);
                    for (uint32_t corner = 0; corner < 3; ++corner)
                    {
                        // Everything except position and normal is inherited: a degenerate sliver
                        // keeps the source normal rather than a normalised zero vector.
                        Vertex vertex = vertices_[firstOriginal[simplified[triangle + corner]]];
                        if (crossLength > 0.0f)
                        {
                            vertex.Normal = crossProduct / crossLength;
                        }
                        emitted[triangle + corner] = static_cast<uint32_t>(vertices_.size());
                        vertices_.push_back(vertex);
                    }
                }
                simplified = std::move(emitted);
            }
            else
            {
                for (uint32_t& index : simplified)
                {
                    index = firstOriginal[index];
                }
            }

            previousIndexCount = resultCount;
            lodIndices_.push_back(std::move(simplified));
        }

        // Synthesised levels grew the vertex buffer; the AABB is unchanged because every new
        // position is one that already existed.
        vertexCount = uint32_t(vertices_.size());
    }

    void Model::FreeMemory()
    {
        vertices_ = std::vector<Vertex>();
        indices_ = std::vector<uint32_t>();
        lodIndices_ = std::vector<std::vector<uint32_t>>();
    }

    Model::Model(const std::string& name, std::vector<Vertex>&& vertices, std::vector<uint32_t>&& indices, bool needGenTSpace) :
        name_(name),
        vertices_(std::move(vertices)),
        indices_(std::move(indices))
    {
        vertexCount = uint32_t(vertices_.size());
        indiceCount = uint32_t(indices_.size());
        
        local_aabb_min = glm::vec3(999999, 999999, 999999);
        local_aabb_max = glm::vec3(-999999, -999999, -999999);

        for( const auto& vertex : vertices_ )
        {
            local_aabb_min = glm::min(local_aabb_min, vertex.Position);
            local_aabb_max = glm::max(local_aabb_max, vertex.Position);
            materialSlotCount = std::max(materialSlotCount, vertex.MaterialIndex + 1);
        }
        
        if(needGenTSpace)
        {
            // mesh processing is expensive, so we cache the result
            XXH64_hash_t verticesHash = XXH64(vertices_.data(), vertices_.size() * sizeof(Vertex), 0);
            XXH64_hash_t indicesHash = XXH64(indices_.data(), indices_.size() * sizeof(uint32_t), 0);
            XXH64_hash_t combinedHash = XXH64(&verticesHash, sizeof(verticesHash), indicesHash);
            
            std::string cacheFileName = Utilities::CookHelper::GetCookedFileName(fmt::format("{:016x}", combinedHash), "tangent");
            if (!std::filesystem::exists(cacheFileName))
            {
                Assets::GenerateMikkTSpace(this);
                SaveTangentCache(cacheFileName);
            }
            else
            {
                LoadTangentCache(cacheFileName);
            }
        }
    }

    void Model::SaveTangentCache(const std::string& cacheFileName)
    {
        std::vector<uint8_t> uncompressedData;
        size_t tangentDataSize = vertices_.size() * sizeof(glm::vec4);
        uncompressedData.resize(tangentDataSize);
        for (size_t i = 0; i < vertices_.size(); ++i)
        {
            std::memcpy(uncompressedData.data() + i * sizeof(glm::vec4), 
                       &vertices_[i].Tangent, sizeof(glm::vec4));
        }
        
        size_t compressedSize = lzav_compress_bound_hi(int32_t(uncompressedData.size()));
        std::vector<uint8_t> compressedData(compressedSize);
        size_t actualCompressedSize = lzav_compress_hi(
            uncompressedData.data(), compressedData.data(),
            int32_t(uncompressedData.size()), int32_t(compressedSize));
        
        if (actualCompressedSize > 0)
        {
            std::ofstream cacheFile(cacheFileName, std::ios::binary);
            if (cacheFile.is_open())
            {
                size_t originalSize = uncompressedData.size();
                cacheFile.write(reinterpret_cast<const char*>(&originalSize), sizeof(size_t));
                cacheFile.write(reinterpret_cast<const char*>(&actualCompressedSize), sizeof(size_t));
                cacheFile.write(reinterpret_cast<const char*>(compressedData.data()), actualCompressedSize);
                cacheFile.close();
            }
        }
    }

    void Model::LoadTangentCache(const std::string& cacheFileName)
    {
        std::ifstream cacheFile(cacheFileName, std::ios::binary);
        if (cacheFile.is_open())
        {
            size_t originalSize, compressedSize;
            cacheFile.read(reinterpret_cast<char*>(&originalSize), sizeof(size_t));
            cacheFile.read(reinterpret_cast<char*>(&compressedSize), sizeof(size_t));
            
            std::vector<uint8_t> compressedData(compressedSize);
            cacheFile.read(reinterpret_cast<char*>(compressedData.data()), compressedSize);
            
            std::vector<uint8_t> uncompressedData(originalSize);
            size_t decompressedSize = lzav_decompress(
                compressedData.data(), uncompressedData.data(),
                int32_t(compressedSize), int32_t(originalSize));
            
            if (decompressedSize == originalSize)
            {
                for (size_t i = 0; i < vertices_.size(); ++i)
                {
                    std::memcpy(&vertices_[i].Tangent,
                               uncompressedData.data() + i * sizeof(glm::vec4),
                               sizeof(glm::vec4));
                }
            }
            
            cacheFile.close();
        }
    }
}
