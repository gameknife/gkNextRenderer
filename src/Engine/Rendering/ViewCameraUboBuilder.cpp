#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Rendering/ViewCameraUboBuilder.hpp"

#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Assets/GPU/Texture.hpp"

namespace Vulkan
{
    namespace
    {
        void FillCameraMatrices(
            Assets::UniformBufferObject& ubo,
            const Assets::Camera& camera,
            const VkExtent2D extent)
        {
            const float aspect = static_cast<float>(std::max(1u, extent.width)) /
                static_cast<float>(std::max(1u, extent.height));
            ubo.ModelView = camera.ModelView;
            ubo.ModelViewInverse = glm::inverse(camera.ModelView);
            ubo.Projection =
                glm::perspective(glm::radians(camera.FieldOfView), aspect, camera.NearPlane, camera.FarPlane);
            ubo.Projection[1][1] *= -1.0f;
            ubo.ProjectionUnJit = ubo.Projection;
            ubo.ProjectionInverse = glm::inverse(ubo.Projection);
            ubo.ProjectionInverseUnJit = ubo.ProjectionInverse;
            ubo.ViewProjection = ubo.Projection * camera.ModelView;
            ubo.ViewProjectionUnJit = ubo.ProjectionUnJit * camera.ModelView;
            ubo.PrevViewProjection = ubo.ViewProjection;
            ubo.PrevViewProjectionUnJit = ubo.ViewProjectionUnJit;
            ubo.Jitter = glm::vec4(0.0f);
            ubo.ViewportRect =
                glm::vec4(0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height));
        }

        void FillSunCascades(
            Assets::UniformBufferObject& ubo,
            const Assets::EnvironmentSetting& env,
            const Assets::Camera& camera,
            const float cascadeDistance)
        {
            const auto cascades = env.ComputeSunCascades(
                ubo.ViewProjectionUnJit, camera.NearPlane, camera.FarPlane, cascadeDistance);
            for (uint32_t i = 0; i < Assets::Scene::kSunShadowCascadeCount; ++i)
            {
                ubo.SunCascadeViewProjection[i] = cascades.viewProjection[i];
            }
            ubo.CascadeSplits = cascades.splits;
        }

        void FillSceneLighting(Assets::UniformBufferObject& ubo, Assets::Scene& scene)
        {
            const Assets::EnvironmentSetting& env = scene.GetEnvSettings();
            const bool hasSun = env.HasSun && env.SunIntensity > 0.0f;
            ubo.SunDirection = glm::vec4(env.SunDirection(), 0.0f);
            ubo.SunColor = hasSun ? glm::vec4(1.0f, 1.0f, 1.0f, 0.0f) * env.SunIntensity : glm::vec4(0.0f);
            ubo.HasSun = hasSun;
            ubo.SkyRotation = env.SkyRotation;
            ubo.SkyIntensity = env.SkyIntensity;
            ubo.SkyIdx = static_cast<uint32_t>(std::max(env.SkyIdx, 0));
            ubo.HasSky = env.HasSky;
            ubo.LightCount = scene.GetLightCount();
        }

        void FillThumbnailDefaults(
            Assets::UniformBufferObject& ubo,
            const Assets::Camera& camera,
            const uint32_t totalFrames)
        {
            ubo.Aperture = camera.Aperture;
            ubo.FocusDistance = camera.FocalDistance;
            ubo.FastGather = false;
            ubo.SuperResolution = 0;
            ubo.MaxNumberOfBounces = 1;
            ubo.NumberOfSamples = 64;
            ubo.NumberOfBounces = 2;
            ubo.TemporalFrames = 1;
            ubo.TotalFrames = totalFrames;
            ubo.TAA = false;
            ubo.ProgressiveRender = false;
            ubo.HDR = false;
            ubo.HDROutputMode = 0;
            ubo.PaperWhiteNit = 200.0f;
            ubo.SceneEpsilonScale = 1.0f;
            ubo.HeatmapScale = 1.0f;
            ubo.ShowHeatmap = false;
            ubo.DebugDraw_Lighting = false;
            ubo.DebugDraw_ShadowCascadeCoverage = false;
            ubo.DenoiseDiffuseSourceSlot = static_cast<uint32_t>(Assets::Bindless::RT_ACCUMLATE_DIFFUSE);
            ubo.DenoiseSpecularSourceSlot = static_cast<uint32_t>(Assets::Bindless::RT_ACCUMLATE_SPECULAR);
            ubo.GTAORadius = 1.0f;
            ubo.GTAOStrength = 0.0f;
            ubo.GTAOThickness = 0.1f;
            ubo.GTAODebugMode = 0;
            ubo.GTAOEnable = false;
            ubo.GTAOQuality = 0;
            ubo.SkyVisEnable = false;
            ubo.SkyVisStrength = 0.0f;
            ubo.SkyVisMaxDistance = 1.0f;
            ubo.SkyVisRayCount = 1;
            ubo.SkyVisCombineMode = 0;
            ubo.SkyVisBlurRadius = 0;
            ubo.SkyVisJitterRadius = 0.0f;
        }
    }

    Assets::UniformBufferObject BuildViewCameraUbo(const FViewCameraUboRequest& request)
    {
        Assets::UniformBufferObject ubo = request.baseUbo != nullptr
            ? *request.baseUbo
            : Assets::UniformBufferObject{};

        FillCameraMatrices(ubo, request.camera, request.extent);
        FillSunCascades(ubo, request.scene.GetEnvSettings(), request.camera, request.cascadeDistance);
        if (request.fillSceneLighting)
        {
            FillSceneLighting(ubo, request.scene);
        }
        if (request.thumbnailDefaults)
        {
            FillThumbnailDefaults(ubo, request.camera, std::max(1u, request.totalFrames));
        }
        else
        {
            ubo.TemporalFrames = 1;
            ubo.TotalFrames = std::max(1u, request.baseUbo != nullptr ? request.baseUbo->TotalFrames : request.totalFrames);
            ubo.TAA = false;
            ubo.ProgressiveRender = false;
        }

        return ubo;
    }

    Assets::Camera BuildOrbitedCamera(
        const Assets::UniformBufferObject& baseUbo,
        const Assets::Camera& camera,
        const float yawDegrees,
        const float pivotDistance)
    {
        Assets::Camera orbitedCamera = camera;
        const glm::mat4 c2w = baseUbo.ModelViewInverse;
        const glm::vec3 eye = glm::vec3(c2w[3]);
        const glm::vec3 forward = -glm::normalize(glm::vec3(c2w[2]));
        const glm::vec3 pivot = eye + forward * pivotDistance;
        const glm::mat4 rot = glm::rotate(glm::mat4(1.0f), glm::radians(yawDegrees), glm::vec3(0, 1, 0));
        const glm::vec3 newEye = pivot + glm::vec3(rot * glm::vec4(eye - pivot, 1.0f));
        orbitedCamera.ModelView = glm::lookAt(newEye, pivot, glm::vec3(0, 1, 0));
        return orbitedCamera;
    }
}
