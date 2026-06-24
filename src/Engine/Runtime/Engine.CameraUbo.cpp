// NextEngine per-frame camera UBO assembly: projection build, TAA Halton
// jitter, Android pre-rotation and sun shadow cascade caching.
// Split from Engine.cpp; same class, separate TU.
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Assets/GPU/Texture.hpp"
#include "Engine/Assets/GPU/UniformBuffer.hpp"
#include "Engine/Options.hpp"
#include "Engine/Rendering/VulkanBaseRenderer.hpp"
#include "Engine/Runtime/GameInstance.hpp"
#include "Engine/Runtime/Config/UserSettings.hpp"
#include "Engine/Vulkan/SwapChain.hpp"

#include <array>
#include <cstdlib>

namespace
{
    constexpr uint32_t maxJitterFrameCount = 256;

    // 生成Halton序列的单一维度
    float HaltonSequence(int index, int base)
    {
        float f = 1.0f;
        float result = 0.0f;
        while (index > 0)
        {
            f = f / base;
            result = result + f * (index % base);
            index = index / base;
        }
        return result;
    }

    const std::array<glm::vec2, maxJitterFrameCount>& HaltonJitterSequence()
    {
        static const std::array<glm::vec2, maxJitterFrameCount> sequence = []
        {
            std::array<glm::vec2, maxJitterFrameCount> result{};
            for (uint32_t i = 0; i < maxJitterFrameCount; ++i)
            {
                const float x = HaltonSequence(static_cast<int>(i + 1), 2);
                const float y = HaltonSequence(static_cast<int>(i + 1), 3);
                result[i] = glm::vec2(x, y) - glm::vec2(0.5f, 0.5f);
            }
            return result;
        }();
        return sequence;
    }

    glm::vec2 GetTemporalJitter(uint32_t frameIndex, uint32_t frameCount)
    {
        frameCount = std::clamp(frameCount, 1u, maxJitterFrameCount);
        return HaltonJitterSequence()[frameIndex % frameCount];
    }
} // namespace

Assets::UniformBufferObject NextEngine::GetUniformBufferObject(const VkOffset2D offset, const VkExtent2D extent)
{
    Assets::UniformBufferObject ubo = {};

    // a copy, simple struct
    Assets::Camera renderCam = scene_->GetRenderCamera();
    gameInstance_->OverrideRenderCamera(renderCam);
    ubo.ModelView = renderCam.ModelView;

    scene_->OverrideModelView(ubo.ModelView);
    ubo.Projection =
        glm::perspective(glm::radians(renderCam.FieldOfView), extent.width / static_cast<float>(extent.height),
                         renderCam.NearPlane, renderCam.FarPlane);

    ubo.FastGather = config_.userSettings.FastGather;
    ubo.SuperResolution = GOption->ReferenceMode ? 2 : config_.userSettings.SuperResolution;
    ubo.Projection[1][1] *= -1;

    glm::mat4x4 projectionUnJit = ubo.Projection;
    // handle android vulkan pre rotation
#if ANDROID
    glm::mat4 pre_rotate_mat = glm::mat4(1.0f);
    glm::vec3 rotation_axis = glm::vec3(0.0f, 0.0f, 1.0f);
    pre_rotate_mat = glm::rotate(pre_rotate_mat, glm::radians(90.0f), rotation_axis);

    ubo.Projection = glm::perspective(glm::radians(renderCam.FieldOfView),
                                      extent.height / static_cast<float>(extent.width), 0.1f, 10000.0f);
    ubo.Projection[1][1] *= -1;
    ubo.Projection = pre_rotate_mat * ubo.Projection;

    projectionUnJit = ubo.Projection;
#endif

    if (config_.userSettings.TAA || config_.userSettings.DLSS)
    {
        const VkExtent2D renderExtent = renderer_->SwapChain().RenderExtent();
        const uint32_t jitterFrames = config_.userSettings.DLSS
            ? config_.userSettings.DLSSJitterFrames
            : config_.userSettings.TemporalFrames;
        glm::vec2 jitter = GetTemporalJitter(frameState_.totalFrames, jitterFrames);
        if (config_.userSettings.DLSSJitterInvertY)
        {
            jitter.y = -jitter.y;
        }

        ubo.Projection[2][0] = jitter.x / static_cast<float>(renderExtent.width) * 2.0f;
        ubo.Projection[2][1] = jitter.y / static_cast<float>(renderExtent.height) * 2.0f;

        ubo.Jitter = glm::vec4(jitter.x, jitter.y, 0, 0);
    }
    else
    {
        ubo.Jitter = glm::vec4(0, 0, 0, 0);
    }

    // Inverting Y for Vulkan, https://matthewwellings.com/blog/the-new-vulkan-coordinate-system/
    ubo.ModelViewInverse = glm::inverse(ubo.ModelView);
    ubo.ProjectionInverse = glm::inverse(ubo.Projection);
    ubo.ViewProjection = ubo.Projection * ubo.ModelView;
    ubo.ViewProjectionUnJit = projectionUnJit * ubo.ModelView;
    ubo.ProjectionUnJit = projectionUnJit;
    ubo.ProjectionInverseUnJit = glm::inverse(projectionUnJit);

    ubo.PrevViewProjection = renderState_.previousUniformBuffer.TotalFrames != 0
                                  ? renderState_.previousUniformBuffer.ViewProjection
                                  : ubo.ViewProjection;
    ubo.PrevViewProjectionUnJit = renderState_.previousUniformBuffer.TotalFrames != 0
                                      ? renderState_.previousUniformBuffer.ViewProjectionUnJit
                                      : ubo.ViewProjectionUnJit;

    ubo.ViewportRect =
        glm::vec4(renderer_->SwapChain().RenderOffset().x, renderer_->SwapChain().RenderOffset().y,
                  renderer_->SwapChain().RenderExtent().width, renderer_->SwapChain().RenderExtent().height);

    const glm::vec4 sunDirection = glm::vec4(scene_->GetEnvSettings().SunDirection(), 0.0f);
    const bool hasSun = scene_->GetEnvSettings().HasSun && scene_->GetEnvSettings().SunIntensity > 0;
    {
        const auto cascades = scene_->GetEnvSettings().ComputeSunCascades(
            ubo.ViewProjectionUnJit, renderCam.NearPlane, renderCam.FarPlane, 400.f);
        const uint32_t frameIndex = static_cast<uint32_t>(std::max(renderer_->FrameCount(), 0));
        const bool forceRefresh = !renderState_.cachedSunCascadesValid ||
                                  (bool)renderState_.previousUniformBuffer.HasSun != hasSun ||
                                  renderState_.previousUniformBuffer.SunDirection != sunDirection;

        if (!hasSun)
        {
            renderState_.cachedSunCascadesValid = false;
            renderState_.sunShadowCascadeUpdateMask = 0u;
            renderState_.sunShadowInitializedMask = 0u;
            renderState_.sunShadowDirtyMask = Assets::Scene::kSunShadowCascadeMask;
        }
        else
        {
            if (!renderState_.cachedSunCascadesValid)
            {
                // 未初始化 cascade 对应的贴图已经被清成 depth=1，先给 UBO 一个有效矩阵。
                renderState_.cachedSunCascades = cascades;
            }
            if (forceRefresh)
            {
                renderState_.sunShadowDirtyMask = Assets::Scene::kSunShadowCascadeMask;
            }

            const uint32_t priorityCascadeMask =
                renderState_.sunShadowDirtyMask |
                (Assets::Scene::kSunShadowCascadeMask & ~renderState_.sunShadowInitializedMask);
            const uint32_t activeCascadeMask =
                Assets::Scene::BuildSunShadowCascadeUpdateMask(frameIndex, priorityCascadeMask);

            for (uint32_t cascade = 0; cascade < Assets::Scene::kSunShadowCascadeCount; ++cascade)
            {
                if ((activeCascadeMask & (1u << cascade)) != 0u)
                {
                    renderState_.cachedSunCascades.viewProjection[cascade] = cascades.viewProjection[cascade];
                    renderState_.cachedSunCascades.splits[cascade] = cascades.splits[cascade];
                }
            }

            renderState_.sunShadowCascadeUpdateMask = activeCascadeMask;
            renderState_.sunShadowInitializedMask |= activeCascadeMask;
            renderState_.sunShadowDirtyMask &= ~activeCascadeMask;
            renderState_.cachedSunCascadesValid = true;
        }

        for (int i = 0; i < 4; ++i)
        {
            ubo.SunCascadeViewProjection[i] = renderState_.cachedSunCascades.viewProjection[i];
        }
        ubo.CascadeSplits = renderState_.cachedSunCascades.splits;
    }

    // Camera Stuff
    ubo.Aperture = renderCam.Aperture;
    ubo.FocusDistance = renderCam.FocalDistance;

    // SceneStuff
    ubo.SkyRotation = scene_->GetEnvSettings().SkyRotation;
    ubo.MaxNumberOfBounces = config_.userSettings.MaxNumberOfBounces;
    ubo.TotalFrames = frameState_.totalFrames;
    ubo.NumberOfSamples = config_.userSettings.NumberOfSamples;
    ubo.NumberOfBounces = config_.userSettings.NumberOfBounces;
    ubo.TAA = config_.userSettings.TAA;
    ubo.SunDirection = sunDirection;
    ubo.SunColor = glm::vec4(1, 1, 1, 0) * scene_->GetEnvSettings().SunIntensity;
    ubo.SkyIntensity = scene_->GetEnvSettings().SkyIntensity;
    ubo.SkyIdx = scene_->GetEnvSettings().SkyIdx;
    ubo.HasSky = scene_->GetEnvSettings().HasSky;
    if (auto* texturePool = Assets::GlobalTexturePool::GetInstance())
    {
        texturePool->TickHDRTextureResidency(
            ubo.SkyIdx, ubo.HasSky, frameState_.totalFrames, config_.userSettings.StreamHDRTextures);
    }
    ubo.HasSun = hasSun;

    if (ubo.HasSun != renderState_.previousUniformBuffer.HasSun ||
        ubo.SunDirection != renderState_.previousUniformBuffer.SunDirection)
    {
        scene_->MarkEnvDirty();
    }

    ubo.ShowHeatmap = config_.showFlags.ShowVisualDebug;
    ubo.HeatmapScale = config_.userSettings.HeatmapScale;
    ubo.DebugDraw_Lighting = config_.showFlags.DebugDraw_Lighting;
    ubo.DebugDraw_ShadowCascadeCoverage = config_.showFlags.DebugDraw_ShadowCascadeCoverage;
    ubo.TemporalFrames = progressiveRender_.enabled ? FProgressiveRenderState::TargetFrames
                                                    : config_.userSettings.TemporalFrames;
    ubo.HDR = renderer_->SwapChain().IsHDR();
    ubo.HDROutputMode = renderer_->SwapChain().HDROutputMode();

    ubo.PaperWhiteNit = config_.userSettings.PaperWhiteNit;
    ubo.LightCount = scene_->GetLightCount();

    // Denoiser routing: when the variance-guided a-trous path is active (denoiser on with a
    // positive iteration count) the compose pass reads the a-trous output; otherwise it reads
    // the temporal accumulation buffers directly (no spatial filtering).
    const bool denoiserOn = IsEffectiveDenoiserEnabled();
    const int diffuseAtrousIterations = denoiserOn ? std::clamp(config_.userSettings.DenoiseAtrousIterations, 0, 6) : 0;
    const int specularAtrousIterations = denoiserOn ? std::clamp(config_.userSettings.DenoiseAtrousSpecularIterations, 0, 6) : 0;
    ubo.DenoiseDiffuseSourceSlot = (diffuseAtrousIterations > 0)
        ? static_cast<uint32_t>(Assets::Bindless::RT_ATROUS_OUT)
        : static_cast<uint32_t>(Assets::Bindless::RT_ACCUMLATE_DIFFUSE);
    ubo.DenoiseSpecularSourceSlot = (specularAtrousIterations > 0)
        ? static_cast<uint32_t>(Assets::Bindless::RT_ATROUS_SPEC_OUT)
        : static_cast<uint32_t>(Assets::Bindless::RT_ACCUMLATE_SPECULAR);
    ubo.GTAORadius = std::max(config_.userSettings.GTAORadius, 0.01f);
    ubo.GTAOStrength = std::max(config_.userSettings.GTAOStrength, 0.0f);
    ubo.GTAOThickness = std::max(config_.userSettings.GTAOThickness, 0.01f);
    ubo.GTAODebugMode = static_cast<uint32_t>(std::clamp(config_.userSettings.GTAODebugMode, 0, 4));
    ubo.GTAOEnable = config_.userSettings.GTAOEnable;
    ubo.GTAOQuality = static_cast<uint32_t>(std::clamp(config_.userSettings.GTAOQuality, 0, 3));

    ubo.SkyVisEnable = config_.userSettings.SkyVisEnable;
    ubo.SkyVisStrength = std::clamp(config_.userSettings.SkyVisStrength, 0.0f, 1.0f);
    ubo.SkyVisMaxDistance = std::max(config_.userSettings.SkyVisMaxDistance, 1.0f);
    ubo.SkyVisRayCount = static_cast<uint32_t>(std::clamp(config_.userSettings.SkyVisRayCount, 1, 64));
    ubo.SkyVisCombineMode = static_cast<uint32_t>(std::clamp(config_.userSettings.SkyVisCombineMode, 0, 2));
    ubo.SkyVisBlurRadius = static_cast<uint32_t>(std::clamp(config_.userSettings.SkyVisBlurRadius, 0, 4));
    ubo.SkyVisJitterRadius = std::clamp(config_.userSettings.SkyVisJitterRadius, 0.0f, 4.0f);

    ubo.ProgressiveRender = progressiveRender_.enabled;
    ubo.SceneEpsilonScale = config_.userSettings.SceneEpsilonScale;
    const float ambientCubeUnit = Assets::SanitizeAmbientCubeUnit(config_.userSettings.AmbientCubeUnit);
    const glm::vec3 ambientCubeOffsetBias =
        glm::vec3(config_.userSettings.AmbientCubeOffsetX, config_.userSettings.AmbientCubeOffsetY,
                  config_.userSettings.AmbientCubeOffsetZ);
    uint32_t ambientCubeCascadeCount =
        Assets::SanitizeAmbientCubeCascadeCount(config_.userSettings.AmbientCubeCascadeCount);
    if (scene_)
    {
        // Never advertise more cascades than the arena was sized for (Phase 2 right-sizing).
        ambientCubeCascadeCount = std::min(ambientCubeCascadeCount, scene_->AmbientCubeCascadeCapacity());
    }
    const float ambientCubeCascadeRatio =
        Assets::SanitizeAmbientCubeCascadeRatio(config_.userSettings.AmbientCubeCascadeRatio);
    ubo.AmbientCubeUnit = ambientCubeUnit;
    ubo.AmbientCubeOffset = Assets::CalculateAmbientCubeOffset(ambientCubeUnit, ambientCubeOffsetBias);
    ubo.AmbientCubeCascadeParams = glm::vec4(float(ambientCubeCascadeCount), ambientCubeCascadeRatio, 0.0f, 0.0f);

    // Other Setup
    renderer_->SetDenoiserEnabled(denoiserOn);
    renderer_->SetVisualDebugEnabled(config_.showFlags.ShowVisualDebug);
    // UBO Backup, for motion vector calc
    renderState_.previousUniformBuffer = ubo;

    return ubo;
}
