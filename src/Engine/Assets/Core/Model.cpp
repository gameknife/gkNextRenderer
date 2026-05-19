#include "Engine/Assets/Core/Model.hpp"
#include "Engine/Utilities/FileHelper.hpp"
#include "Engine/Assets/Loaders/FSceneLoader.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/hash.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>

#include <chrono>
#include <filesystem>
#include <fmt/format.h>

#include <xxhash.h>

#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Subsystems/NextPhysics.h"

#define PROVOKING_VERTICE 1

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
    template <typename T>
    T AnimationChannel<T>::Sample(float time)
    {
        for ( int i = 0; i < Keys.size() - 1; i++ )
        {
            auto& key = Keys[i];
            auto& keyNext = Keys[i + 1];
            if (time >= key.Time && time < keyNext.Time)
            {
                float t = (time - key.Time) / (keyNext.Time - key.Time);
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

    // 偏特化T == glm::quat
    template <>
    glm::quat AnimationChannel<glm::quat>::Sample(float time)
    {
        for ( int i = 0; i < Keys.size() - 1; i++ )
        {
            auto& key = Keys[i];
            auto& keyNext = Keys[i + 1];
            if (time >= key.Time && time < keyNext.Time)
            {
                float t = (time - key.Time) / (keyNext.Time - key.Time);
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

    void Model::FreeMemory()
    {
        vertices_ = std::vector<Vertex>();
        indices_ = std::vector<uint32_t>();
    }

    Model::Model(const std::string& name, std::vector<Vertex>&& vertices, std::vector<uint32_t>&& indices, bool needGenTSpace) :
        name_(name),
        vertices_(std::move(vertices)),
        indices_(std::move(indices))
    {
        verticeCount = uint32_t(vertices_.size());
        indiceCount = uint32_t(indices_.size());
        
        local_aabb_min = glm::vec3(999999, 999999, 999999);
        local_aabb_max = glm::vec3(-999999, -999999, -999999);

        for( const auto& vertex : vertices_ )
        {
            local_aabb_min = glm::min(local_aabb_min, vertex.Position);
            local_aabb_max = glm::max(local_aabb_max, vertex.Position);
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
                Assets::FSceneLoader::GenerateMikkTSpace(this);
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
