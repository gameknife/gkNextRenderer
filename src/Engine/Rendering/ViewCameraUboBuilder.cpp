#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Rendering/ViewCameraUboBuilder.hpp"

// Camera UBO assembly is part of the concrete renderer implementation pack.
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Utilities/Math.hpp"

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
            ubo.Projection = Utilities::Math::ReverseZPerspective(
                glm::radians(camera.FieldOfView), aspect, camera.NearPlane, camera.FarPlane);
            ubo.Projection[1][1] *= -1.0f;
            ubo.ProjectionUnJit = ubo.Projection;
#if ANDROID
            glm::mat4 preRotate = glm::mat4(1.0f);
            preRotate = glm::rotate(preRotate, glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
            ubo.Projection = Utilities::Math::ReverseZPerspective(
                glm::radians(camera.FieldOfView),
                static_cast<float>(std::max(1u, extent.height)) / static_cast<float>(std::max(1u, extent.width)),
                0.1f,
                10000.0f);
            ubo.Projection[1][1] *= -1.0f;
            ubo.Projection = preRotate * ubo.Projection;
            ubo.ProjectionUnJit = ubo.Projection;
#endif
            ubo.ProjectionInverse = glm::inverse(ubo.Projection);
            ubo.ProjectionInverseUnJit = ubo.ProjectionInverse;
            ubo.ViewProjection = ubo.Projection * camera.ModelView;
            ubo.ViewProjectionUnJit = ubo.ProjectionUnJit * camera.ModelView;
            ubo.PrevViewProjection = ubo.ViewProjection;
            ubo.PrevViewProjectionUnJit = ubo.ViewProjectionUnJit;
            ubo.Jitter = glm::vec4(0.0f);
            ubo.CheckerboardSparseLighting = false;
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
            ubo.SunColor =
                hasSun ? glm::vec4(env.SunColor, 0.0f) * env.SunIntensity : glm::vec4(0.0f);
            ubo.SkyColor = glm::vec4(env.SkyColor, 1.0f);
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
            ubo.ExitAfterFirst = false;
            ubo.SuperResolution = 0;
            ubo.MaxNumberOfBounces = 1;
            ubo.NumberOfSamples = 64;
            ubo.NumberOfBounces = 2;
            ubo.TemporalFrames = 1;
            ubo.TotalFrames = totalFrames;
            ubo.PrimaryRayJitter = false;
            ubo.ProgressiveRender = false;
            ubo.HDR = false;
            ubo.HDROutputMode = 0;
            ubo.PaperWhiteNit = 200.0f;
            ubo.SceneEpsilonScale = 1.0f;
            ubo.HeatmapScale = 1.0f;
            ubo.ShowHeatmap = false;
            ubo.DebugDraw_Lighting = false;
            ubo.ForceBlackBackground = false;
            ubo.GTAORadius = 1.0f;
            ubo.GTAOStrength = 0.0f;
            ubo.GTAOThickness = 0.1f;
            ubo.GTAODebugMode = 0;
            ubo.GTAOEnable = false;
            ubo.GTAOQuality = 0;
            ubo.LightObjectScreenSpaceShadow = false;
            ubo.LightObjectShadowDistance = 0.0f;
            ubo.LightObjectMaxShadowedLights = 0;
            ubo.LightObjectShadowSteps = 4;
            // Thumbnails render off the primary camera's grid anchor, so disable the grid for them
            // and let every light query take the global-CDF fallback.
            ubo.LightGridCascadeCount = 0;
            ubo.LightGridCullThreshold = 0.0f;
            ubo.LightGridAnchor = glm::vec4(0.0f);
        }
    }

    Assets::UniformBufferObject BuildViewCameraUbo(const FViewCameraUboRequest& request)
    {
        Assets::UniformBufferObject ubo = request.baseUbo != nullptr
            ? *request.baseUbo
            : Assets::UniformBufferObject{};
        if (request.baseUbo == nullptr)
        {
            // Value-initialization zeroes the UBO, and a zero indirect multiplier would silently
            // black out every bounce in this view. Views without a base inherit the physical default.
            ubo.IndirectIntensity = 1.0f;
            ubo.MultiBounceIntensity = 1.0f;
        }

        FillCameraMatrices(ubo, request.camera, request.extent);
        if (request.fillSunCascades)
        {
            FillSunCascades(ubo, request.scene.GetEnvSettings(), request.camera, request.cascadeDistance);
        }
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
            ubo.PrimaryRayJitter = false;
            ubo.ProgressiveRender = false;
        }

        return ubo;
    }
}
