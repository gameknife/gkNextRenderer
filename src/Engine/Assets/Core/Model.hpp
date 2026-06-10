#pragma once
#include "Engine/Assets/Data/Vertex.hpp"
#include "Engine/Assets/GPU/Texture.hpp"
#include "Engine/Assets/GPU/UniformBuffer.hpp"
#include <glm/ext.hpp>

#include <array>

struct FNextPhysicsBody;

namespace Assets
{
    struct Camera final
    {
        std::string name;
        glm::mat4 ModelView;
        float FieldOfView;
        float Aperture;
        float FocalDistance;
        float NearPlane = 0.2f;
        float FarPlane = 2000.0f;
    };

    struct CascadeShadowSetup
    {
        std::array<glm::mat4, 4> viewProjection{};
        glm::vec4 splits{};   // view-space positive distance at the far edge of each cascade
    };

    struct EnvironmentSetting
    {
        EnvironmentSetting()
        {
            Reset();
        }
        
        void Reset()
        {
            ControlSpeed = 5.0f;
            GammaCorrection = true;
            HasSky = true;
            HasSun = false;
            SkyIdx = 0;
            SunIntensity = 500.f;
            SkyIntensity = 100.0f;
            SkyRotation = 0;
            SunRotation = 0.5f;   
        }

        glm::vec3 SunDirection() const 
        {
            return glm::normalize(glm::vec3( sinf( SunRotation * glm::pi<float>() ), 0.75f, cosf(SunRotation * glm::pi<float>()) ));
        }

        // Deprecated: 旧 CPU shadowmap 路径用。新的 GPU CSM 走 ComputeSunCascades。
        glm::mat4 GetSunViewProjection() const
        {
            // 获取阳光方向并规范化
            vec3 lightDir = normalize(-SunDirection());

            // 计算向上向量（确保不与光线方向共线）
            vec3 lightUp = abs(lightDir.y) > 0.99f ? vec3(1.0f, 0.0f, 0.0f) : vec3(0.0f, 1.0f, 0.0f);

            // 计算右向量和新的上向量（确保三个向量互相垂直）
            vec3 lightRight = normalize(cross(lightUp, lightDir));
            lightUp = normalize(cross(lightDir, lightRight));

            // 定义阴影图覆盖的世界空间大小
            float halfSize = 100.f;

            // 构建从光源视角的观察矩阵（将光源放在远处）
            vec3 lightPos = vec3(0) - lightDir * 1000.f;
            mat4 lightView = glm::lookAt(lightPos, lightPos + lightDir, lightUp);

            // 创建正交投影矩阵
            mat4 lightProj = glm::ortho(-halfSize, halfSize, -halfSize, halfSize, 500.f, 2000.f);

            // 返回组合的视图投影矩阵
            return lightProj * lightView;
        }

        // GPU CSM：以主相机视椎为根据切 4 段，对每段计算光源 view-proj。
        // cameraViewProj 应为未抖动的主相机 view*proj；shadowFar 控制 cascade 覆盖深度。
        CascadeShadowSetup ComputeSunCascades(
            const glm::mat4& cameraViewProj,
            float cameraNear,
            float cameraFar,
            float shadowFar = 100.0f) const;
        
        float ControlSpeed;
        bool GammaCorrection;
        bool HasSky;
        bool HasSun;
        int32_t SkyIdx;
        float SunRotation;
        float SkyRotation;
        
        float SkyIntensity = 100.0f;
        float SunIntensity = 500.0f;

        std::vector<Camera> cameras;
    };

    template <typename T>
    struct AnimationKey
    {
        float Time;
        T Value;
    };

    template <typename T>
    struct AnimationChannel
    {
        std::vector<AnimationKey<T>> Keys;
        T Sample(float time);
    };
    
    struct AnimationTrack
    {
        bool Playing() const { return Playing_; }
        void Play() { Playing_ = true; }
        void Stop() { Playing_ = false; }
        
        void Sample(float time, glm::vec3& translation, glm::quat& rotation, glm::vec3& scaling);
        
        std::string AnimationName;
        std::string NodeName_;
        
        AnimationChannel<glm::vec3> TranslationChannel;
        AnimationChannel<glm::quat> RotationChannel;
        AnimationChannel<glm::vec3> ScaleChannel;
        
        float Time_;
        float Duration_;
        float PlaySpeed_ = 1.0f;

        bool Playing_{};
    };
    
    class Model final
    {
    public:
        Model& operator =(const Model&) = delete;
        Model& operator =(Model&&) = delete;

        Model() = default;
        Model(const Model&) = default;
        Model(Model&&) = default;
        ~Model() = default;

        const std::string& Name() const { return name_; }
        const std::vector<Vertex>& CPUVertices() const { return vertices_; }
        std::vector<Vertex>& CPUVertices() { return vertices_; }
        const std::vector<uint32_t>& CPUIndices() const { return indices_; }

        const std::vector<glm::vec4>& CPUWeights() const { return weights_; }
        std::vector<glm::vec4>& CPUWeights() { return weights_; }
        const std::vector<glm::uvec4>& CPUJoints() const { return joints_; }
        std::vector<glm::uvec4>& CPUJoints() { return joints_; }
        
        glm::vec3 GetLocalAABBMin() const {return local_aabb_min;}
        glm::vec3 GetLocalAABBMax() const {return local_aabb_max;}

        uint32_t NumberOfVertices() const { return verticeCount; }
        uint32_t NumberOfIndices() const { return indiceCount; }
        uint32_t SectionCount() const { return sectionCount; }
        void SetSectionCount(uint32_t count) { sectionCount = count; }

        void FreeMemory();

        // Public factory for external mesh loaders (modules) that build models
        // from raw vertex/index streams without friend access.
        static Model CreateFromGeometry(const std::string& name, std::vector<Vertex>&& vertices,
                                        std::vector<uint32_t>&& indices, bool needGenTSpace = true)
        {
            return Model(name, std::move(vertices), std::move(indices), needGenTSpace);
        }

    private:
        Model(const std::string& name, std::vector<Vertex>&& vertices, std::vector<uint32_t>&& indices, bool needGenTSpace = true);

        void SaveTangentCache(const std::string& cacheFileName);
        void LoadTangentCache(const std::string& cacheFileName);

        std::string name_;
        
        std::vector<Vertex> vertices_;
        std::vector<uint32_t> indices_;
        std::vector<glm::vec4> weights_;
        std::vector<glm::uvec4> joints_;
        
        glm::vec3 local_aabb_min;
        glm::vec3 local_aabb_max;

        uint32_t verticeCount;
        uint32_t indiceCount;

        uint32_t sectionCount;

        friend class FProcModel;
        friend class FSceneLoader;
    };
}
