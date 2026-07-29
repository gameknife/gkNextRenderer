#pragma once
#include "Engine/Assets/Data/Vertex.hpp"
#include "Engine/Assets/GPU/Texture.hpp"
#include "Engine/Assets/GPU/UniformBuffer.hpp"
#include <glm/ext.hpp>

#include <array>
#include <cmath>

namespace Assets
{
    struct AtmosphereSetting
    {
        AtmosphereSetting()
        {
            Reset();
        }

        void Reset()
        {
            RayleighScattering = glm::vec3(0.005802f, 0.013558f, 0.033100f);
            RayleighDensityH = 8.0f;
            MieScattering = glm::vec3(0.003996f);
            MieDensityH = 1.2f;
            MieAbsorption = glm::vec3(0.004440f);
            MiePhaseG = 0.8f;
            OzoneAbsorption = glm::vec3(0.000650f, 0.001881f, 0.000085f);
            OzoneCenterAltitude = 25.0f;
            GroundAlbedo = glm::vec3(0.3f);
            OzoneWidth = 15.0f;
            BottomRadius = 6360.0f;
            TopRadius = 6460.0f;
            WorldUnitsPerKm = 1000.0f;
            WorldOriginAltitude = 0.0f;
            AerialPerspectiveMaxDistance = 20000.0f;
            SkyLuminanceScale = 1.0f;
            FogInscatteringColor = glm::vec3(0.55f, 0.65f, 0.75f);
            FogDensity = 0.01f;
            FogHeightFalloff = 0.2f;
            FogBaseHeight = 0.0f;
            FogStartDistance = 0.0f;
            FogMaxOpacity = 0.95f;
        }

        glm::vec3 RayleighScattering{};
        float RayleighDensityH{};
        glm::vec3 MieScattering{};
        float MieDensityH{};
        glm::vec3 MieAbsorption{};
        float MiePhaseG{};
        glm::vec3 OzoneAbsorption{};
        float OzoneCenterAltitude{};
        glm::vec3 GroundAlbedo{};
        float OzoneWidth{};
        float BottomRadius{};
        float TopRadius{};
        float WorldUnitsPerKm{};
        float WorldOriginAltitude{};
        float AerialPerspectiveMaxDistance{};
        float SkyLuminanceScale{};
        glm::vec3 FogInscatteringColor{};
        float FogDensity{};
        float FogHeightFalloff{};
        float FogBaseHeight{};
        float FogStartDistance{};
        float FogMaxOpacity{};
    };

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
            SunColor = glm::vec3(1.0f);
            SkyColor = glm::vec3(1.0f);
            SkyRotation = 0;
            SunRotation = 0.5f;   
            SunElevation = std::atan(0.75f);
        }

        glm::vec3 SunDirection() const 
        {
            const float azimuth = SunRotation * glm::pi<float>();
            const float horizontalScale = std::cos(SunElevation);
            return glm::normalize(glm::vec3(
                std::sin(azimuth) * horizontalScale,
                std::sin(SunElevation),
                std::cos(azimuth) * horizontalScale));
        }

        // Deprecated: retained for the old CPU shadow-map path. GPU CSM uses ComputeSunCascades.
        glm::mat4 GetSunViewProjection() const
        {
            // Normalize the sun direction.
            vec3 lightDir = normalize(-SunDirection());

            // Choose an up vector that is not collinear with the light direction.
            vec3 lightUp = abs(lightDir.y) > 0.99f ? vec3(1.0f, 0.0f, 0.0f) : vec3(0.0f, 1.0f, 0.0f);

            // Derive an orthogonal right vector and corrected up vector.
            vec3 lightRight = normalize(cross(lightUp, lightDir));
            lightUp = normalize(cross(lightDir, lightRight));

            // Define the world-space area covered by the shadow map.
            float halfSize = 100.f;

            // Build the light-view matrix with the light positioned far away.
            vec3 lightPos = vec3(0) - lightDir * 1000.f;
            mat4 lightView = glm::lookAt(lightPos, lightPos + lightDir, lightUp);

            // Build the orthographic projection matrix.
            mat4 lightProj = glm::ortho(-halfSize, halfSize, -halfSize, halfSize, 500.f, 2000.f);

            // Return the combined view-projection matrix.
            return lightProj * lightView;
        }

        // GPU CSM: split the main-camera frustum into four ranges and compute a light view-projection for each.
        // cameraViewProj must be the unjittered main-camera view * projection; shadowFar controls cascade depth.
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
        float SunElevation;
        float SkyRotation;
        
        float SkyIntensity = 100.0f;
        float SunIntensity = 500.0f;
        glm::vec3 SunColor{1.0f};
        glm::vec3 SkyColor{1.0f};
        AtmosphereSetting Atmosphere;

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
        T Sample(float time) const;
    };

    template <>
    glm::quat AnimationChannel<glm::quat>::Sample(float time) const;
    extern template glm::vec3 AnimationChannel<glm::vec3>::Sample(float time) const;

    struct AnimationTrack
    {
        enum class Target
        {
            NodeTransform,
            Environment
        };

        bool Playing() const { return Playing_; }
        void Play() { Playing_ = true; }
        void Stop() { Playing_ = false; }
        
        void Sample(float time, glm::vec3& translation, glm::quat& rotation, glm::vec3& scaling);
        void Sample(float time, EnvironmentSetting& environment);
        
        std::string AnimationName;
        std::string NodeName_;
        Target Target_ = Target::NodeTransform;
        
        AnimationChannel<glm::vec3> TranslationChannel;
        AnimationChannel<glm::quat> RotationChannel;
        AnimationChannel<glm::vec3> ScaleChannel;
        AnimationChannel<float> SunRotationChannel;
        AnimationChannel<float> SunElevationChannel;
        AnimationChannel<float> SkyRotationChannel;
        AnimationChannel<float> SunIntensityChannel;
        AnimationChannel<float> SkyIntensityChannel;
        AnimationChannel<glm::vec3> SunColorChannel;
        AnimationChannel<glm::vec3> SkyColorChannel;
        
        float Time_ = 0.0f;
        float Duration_ = 0.0f;
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

        uint32_t NumberOfVertices() const { return vertexCount; }
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

        uint32_t vertexCount;
        uint32_t indiceCount;

        uint32_t sectionCount;

        friend class FProcModel;
        friend class FSceneLoader;
    };
}
