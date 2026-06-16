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

#include <cstdlib>

namespace
{
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

    // 生成2D Halton序列
    std::vector<glm::vec2> GenerateHaltonSequence(int count)
    {
        std::vector<glm::vec2> sequence;
        for (int i = 0; i < count; ++i)
        {
            float x = HaltonSequence(i + 1, 2); // 基数2
            float y = HaltonSequence(i + 1, 3); // 基数3
            sequence.push_back(glm::vec2(x, y));
        }
        return sequence;
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
    ubo.SelectedId = scene_->GetSelectedId();
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
        std::vector<glm::vec2> haltonSeq = GenerateHaltonSequence(config_.userSettings.TemporalFrames);
        glm::vec2 jitter =
            haltonSeq[frameState_.totalFrames % config_.userSettings.TemporalFrames] - glm::vec2(0.5f, 0.5f);

        ubo.Projection[2][0] = jitter.x / static_cast<float>(extent.width) * 2.0f;
        ubo.Projection[2][1] = jitter.y / static_cast<float>(extent.height) * 2.0f;

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

    ubo.SelectedId = scene_->GetSelectedId();

    // Camera Stuff
    ubo.Aperture = renderCam.Aperture;
    ubo.FocusDistance = renderCam.FocalDistance;

    // SceneStuff
    ubo.SkyRotation = scene_->GetEnvSettings().SkyRotation;
    ubo.MaxNumberOfBounces = config_.userSettings.MaxNumberOfBounces;
    ubo.TotalFrames = frameState_.totalFrames;
    ubo.NumberOfSamples = config_.userSettings.NumberOfSamples;
    ubo.NumberOfBounces = config_.userSettings.NumberOfBounces;
    ubo.AdaptiveSample = config_.userSettings.AdaptiveSample;
    ubo.AdaptiveVariance = config_.userSettings.AdaptiveVariance;
    ubo.AdaptiveSteps = config_.userSettings.AdaptiveSteps;
    ubo.TAA = config_.userSettings.TAA;
    ubo.RandomSeed = rand();
    ubo.SunDirection = sunDirection;
    ubo.SunColor = glm::vec4(1, 1, 1, 0) * scene_->GetEnvSettings().SunIntensity;
    ubo.SkyIntensity = scene_->GetEnvSettings().SkyIntensity;
    ubo.SkyIdx = scene_->GetEnvSettings().SkyIdx;
    ubo.BackGroundColor = glm::vec4(0.4, 0.6, 1.0, 0.0) * 4.0f * scene_->GetEnvSettings().SkyIntensity;
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
    ubo.UseCheckerBoard = config_.userSettings.UseCheckerBoardRendering;
    ubo.TemporalFrames = progressiveRender_.enabled ? 256 : config_.userSettings.TemporalFrames;
    ubo.HDR = renderer_->SwapChain().IsHDR();

    ubo.PaperWhiteNit = config_.userSettings.PaperWhiteNit;
    ubo.LightCount = scene_->GetLightCount();

    ubo.BFSigma = config_.userSettings.DenoiseSigma;
    ubo.BFSigmaLum = config_.userSettings.DenoiseSigmaLum;
    ubo.BFSigmaNormal = config_.userSettings.DenoiseSigmaNormal;
    ubo.BFSigmaDepth = config_.userSettings.DenoiseSigmaDepth;

    // Denoiser routing: when the variance-guided a-trous path is active it does the spatial
    // filtering, so the JBF in the compose pass is disabled (BFSize = 0) and compose reads the
    // a-trous output. With the denoiser on but a-trous disabled, fall back to the JBF (Phase 0).
    const bool denoiserOn = config_.userSettings.Denoiser;
    const int atrousIterations = denoiserOn ? std::clamp(config_.userSettings.DenoiseAtrousIterations, 0, 6) : 0;
    ubo.BFSize = (denoiserOn && atrousIterations == 0) ? config_.userSettings.DenoiseSize : 0;
    ubo.DenoiseDiffuseSourceSlot = (atrousIterations > 0)
        ? static_cast<uint32_t>(Assets::Bindless::RT_ATROUS_OUT)
        : static_cast<uint32_t>(Assets::Bindless::RT_ACCUMLATE_DIFFUSE);
    ubo.DenoiseSpecularSourceSlot = (atrousIterations > 0)
        ? static_cast<uint32_t>(Assets::Bindless::RT_ATROUS_SPEC_OUT)
        : static_cast<uint32_t>(Assets::Bindless::RT_ACCUMLATE_SPECULAR);

    ubo.ShowEdge = config_.showFlags.ShowEdge;
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
    renderer_->SetDenoiserEnabled(config_.userSettings.Denoiser);
    renderer_->SetVisualDebugEnabled(config_.showFlags.ShowVisualDebug);
    // UBO Backup, for motion vector calc
    renderState_.previousUniformBuffer = ubo;

    return ubo;
}
