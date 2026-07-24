implementing PathTracing;

#include "PreProcessor.slang"

// Direct-lighting layer shared by the path tracer and software tracing:
//   - Common sun-disk + finite/delta-light NEE sampling helpers (SelectLight,
//     EvaluateLightDirectSample / EvaluateLightDiffuseSample, AccumulateSunBSDF, ...)
//   - Common CSM sun-shadow sampling (SelectSunCascade / SampleSunShadowCSM)
//   - FShadowMapDirectIlluminator : SoftwareModern sun-only Lambert + CSM
//   - ISunShadowPolicy + FTracingDirectIlluminator<TTracer, TSun>, aliased as
//     FHardwareDirectIlluminator (ray-query) and FSoftwareTracingDirectIlluminator
//     (CSM sun + voxel-DDA visibility)
// The multi-bounce FPathTracingRenderer that drives these lives in
// PathTracingRenderer.slang.

namespace Common
{
    // CDF light selection shared by classic light NEE and ReSTIR candidate generation
    // (reserved1 carries the cdf built in Scene::UpdateLights).
    public uint SelectLight(float lightSelector)
    {
        GPUScene gpuScene = Bindless.GetGpuscene();
        UniformBufferObject Camera = gpuScene.Camera[0];
        uint lightIndex = Camera.LightCount - 1;
        for (uint i = 0; i < Camera.LightCount; ++i)
        {
            if (lightSelector < asfloat(gpuScene.Lights[i].reserved1))
            {
                lightIndex = i;
                break;
            }
        }
        return lightIndex;
    }

    public float GetLightSampleMeasure(LightObject light)
    {
        if (light.lightType == LightTypePoint)
        {
            return 1.0f;
        }
        return light.lightType == LightTypeArea ? light.normal_area.w : 0.0f;
    }

    public float2 CanonicalizeLightUV(LightObject light, float2 uv)
    {
        return light.lightType == LightTypePoint ? float2(0, 0) : uv;
    }

    // The visible marker of an analytic point light is not an occluder of that light.
    // Shorten only the visibility segment; illumination still uses the analytic center.
    public float3 GetLightVisibilityTarget(uint lightIndex, float3 rayOrigin,
                                           float3 lightPosition)
    {
        LightObject light = Bindless.GetGpuscene().Lights[lightIndex];
        if (light.lightType != LightTypePoint || light.p0.w <= 0.0f)
        {
            return lightPosition;
        }
        const float3 segment = lightPosition - rayOrigin;
        const float distance = length(segment);
        if (distance <= EPS)
        {
            return lightPosition;
        }
        return lightPosition - segment * (min(light.p0.w, distance * 0.5f) / distance);
    }

    // BSDF-aware finite/delta-light sample. Diffuse stays albedo-demodulated while
    // specular is complete radiance; lightPdf is the discrete light-selection probability.
    public bool EvaluateLightDirectSample(uint lightIndex, float2 uv, FBSDFContext context,
                                          out FDirectLighting direct,
                                          out float3 lightPosition)
    {
        direct = FDirectLighting();
        lightPosition = float3(0, 0, 0);
        GPUScene gpuScene = Bindless.GetGpuscene();
        LightObject light = gpuScene.Lights[lightIndex];
        const float sampleMeasure = GetLightSampleMeasure(light);
        const float lightPdf = asfloat(light.reserved2);
        if (sampleMeasure <= 0.0f || lightPdf <= 0.0f)
        {
            return false;
        }
        Material lightMaterial = gpuScene.GetMaterial(light.lightMatIdx);
        if (lightMaterial.MaterialModel != MaterialDiffuseLight)
        {
            return false;
        }

        if (light.lightType == LightTypePoint)
        {
            lightPosition = light.p0.xyz;
        }
        else
        {
            lightPosition = light.p0.xyz + uv.x * (light.p1.xyz - light.p0.xyz) +
                            uv.y * (light.p3.xyz - light.p0.xyz);
        }
        const float3 toLight = lightPosition - context.Position;
        const float distanceSquared = dot(toLight, toLight);
        if (distanceSquared <= EPS * EPS)
        {
            return false;
        }
        const float3 wi = toLight * rsqrt(distanceSquared);
        const float surfaceCosine = max(dot(context.Normal, wi), 0.0f);
        if (surfaceCosine <= 0.0f)
        {
            return false;
        }

        FBSDFEval bsdf = EvaluateSurfaceBSDF(context, wi);
        float geometryAndPdf;
        float specularMis = 1.0f;
        if (light.lightType == LightTypePoint)
        {
            // DiffuseLight RGB is radiant intensity for a point record. A point is a
            // delta-direction proposal, so a finite-density BSDF proposal cannot overlap it.
            geometryAndPdf = surfaceCosine / (distanceSquared * lightPdf);
        }
        else
        {
            const float lightCosine =
                max(dot(normalize(light.normal_area.xyz), -wi), 0.0f);
            if (lightCosine <= 0.0f)
            {
                return false;
            }
            const float lightPdfOmega =
                (lightPdf / sampleMeasure) * distanceSquared / lightCosine;
            specularMis = PowerHeuristic(
                lightPdfOmega, EvaluateGlossyProposalPdf(context, wi));
            geometryAndPdf = surfaceCosine * lightCosine * sampleMeasure /
                             (distanceSquared * lightPdf);
        }
        direct.DiffuseDemodulated = lightMaterial.Diffuse.rgb *
                                     bsdf.DiffuseDemodulated * geometryAndPdf;
        // Exact/near-delta reflection is handled by a direction sample in the renderer.
        if (context.PerceptualRoughness > BSDF_DELTA_ROUGHNESS)
        {
            direct.SpecularRadiance = lightMaterial.Diffuse.rgb * bsdf.Specular *
                                      geometryAndPdf * specularMis;
        }
        return any(direct.DiffuseDemodulated > float3(0, 0, 0)) ||
               any(direct.SpecularRadiance > float3(0, 0, 0));
    }

    // ReSTIR target/shading payload: BSDF-aware diffuse only, excluding the full
    // source PDF (discrete light probability divided by finite sample measure).
    public bool EvaluateLightDiffuseSample(uint lightIndex, float2 uv, FBSDFContext context,
                                           out float3 f, out float3 lightPosition)
    {
        f = float3(0, 0, 0);
        FDirectLighting direct;
        if (!EvaluateLightDirectSample(lightIndex, uv, context, direct, lightPosition))
        {
            return false;
        }
        LightObject light = Bindless.GetGpuscene().Lights[lightIndex];
        const float sampleMeasure = GetLightSampleMeasure(light);
        const float lightPdf = asfloat(light.reserved2);
        if (sampleMeasure <= 0.0f || lightPdf <= 0.0f)
        {
            return false;
        }
        f = direct.DiffuseDemodulated * (lightPdf / sampleMeasure);
        return any(f > float3(0, 0, 0));
    }

    // The sun is modeled as a finite disk of angular radius 0.25 degrees. Sun sampling is
    // uniform over that cone, so a single cosine threshold, solid angle and sample pdf are
    // shared by every sun NEE / analytic-disk / MIS site below.
    public float SunCosAngularRadius() { return cos(0.25f / 180.0f * M_PI); }
    public float SunSolidAngle() { return M_TWO_PI * max(1.0f - SunCosAngularRadius(), 1e-7f); }
    public float SunSamplePdf() { return 1.0f / SunSolidAngle(); }

    public float3 SampleSunDirection(inout uint4 randomSeed, float3 sunDirection)
    {
        return AlignWithNormal(RandomInCone(randomSeed, SunCosAngularRadius()),
                               normalize(sunDirection));
    }

    public void AccumulateSunBSDF(FBSDFContext context, float3 wi, float visibility,
                                  float3 sunColor, inout FDirectLighting direct)
    {
        if (visibility <= 0.0f)
        {
            return;
        }
        FBSDFEval bsdf = EvaluateSurfaceBSDF(context, wi);
        const float NoL = max(dot(context.Normal, wi), 0.0f);
        direct.DiffuseDemodulated += sunColor * bsdf.DiffuseDemodulated * NoL * visibility;
        if (context.PerceptualRoughness > BSDF_DELTA_ROUGHNESS)
        {
            const float specularMis = PowerHeuristic(
                SunSamplePdf(), EvaluateGlossyProposalPdf(context, wi));
            direct.SpecularRadiance += sunColor * bsdf.Specular * NoL * visibility * specularMis;
        }
    }

    public float3 EvaluateAnalyticSunDisk(float3 rayDirection)
    {
        UniformBufferObject camera = Bindless.GetGpuscene().Camera[0];
        if (!camera.HasSun)
        {
            return float3(0, 0, 0);
        }
        if (dot(normalize(rayDirection), normalize(camera.SunDirection.xyz)) <
            SunCosAngularRadius())
        {
            return float3(0, 0, 0);
        }
        return camera.SunColor.rgb / SunSolidAngle();
    }
}

// FHardwareDirectIlluminator and FSoftwareTracingDirectIlluminator are type aliases of the
// generic FTracingDirectIlluminator, defined after the CSM helpers below (which the software
// sun-shadow policy depends on).

namespace Common
{
    // 基于 view-space 距离选 cascade（CascadeSplits 为正向距离）
    public uint SelectSunCascade(float viewDist, float4 splits)
    {
        if (viewDist < splits.x) return 0;
        if (viewDist < splits.y) return 1;
        if (viewDist < splits.z) return 2;
        return 3;
    }

    // 3x3 PCF + slope-scaled bias，对 4 个 CSM cascade 都通用。
    // 返回 [0,1]，1 = 完全无遮蔽，0 = 完全阴影。
    public float SampleSunShadowCSM(float3 worldPos, float3 normal, float viewDist)
    {
        UniformBufferObject Camera = Bindless.GetGpuscene().Camera[0];
        uint c = Common.SelectSunCascade(viewDist, Camera.CascadeSplits);

        float4 lightPos = mul(Camera.SunCascadeViewProjection[c], float4(worldPos, 1.0));
        float3 projCoords = lightPos.xyz / lightPos.w;
        float2 uv = projCoords.xy * 0.5 + 0.5;

        // 出 cascade 视椎视为无遮挡
        if (any(uv < float2(0.0)) || any(uv > float2(1.0)))
            return 1.0f;
        if (projCoords.z < 0.0f || projCoords.z > 1.0f)
            return 1.0f;

        float NoL = max(dot(normalize(normal), Camera.SunDirection.xyz), 0.0f);
        float bias = max(0.0008f * (1.0f - NoL), 0.0001f);

        Sampler2D shadowMap = Bindless.GetShadowMap(int(c));
        uint2 texSize;
        shadowMap.GetDimensions(texSize.x, texSize.y);
        float2 texelSize = 1.0f / float2(texSize);

        float shadow = 0.0f;
        [unroll]
        for (int dy = -1; dy <= 1; ++dy)
        {
            [unroll]
            for (int dx = -1; dx <= 1; ++dx)
            {
                float2 offset = float2(dx, dy) * texelSize;
                float closestDepth = shadowMap.SampleLevel(uv + offset, 0).r;
                shadow += (projCoords.z - bias) > closestDepth ? 0.0f : 1.0f;
            }
        }
        return shadow * (1.0f / 9.0f);
    }
}

public struct FShadowMapDirectIlluminator : IDirectIlluminator
{
    public void DirectIlluminate(inout uint4 RandomSeed, FBSDFContext context,
                                 inout FDirectLighting directLighting)
    {
        UniformBufferObject Camera = Bindless.GetGpuscene().Camera[0];
        const float3 lightVector = Camera.SunDirection.xyz;
        const float3 d = max(dot(lightVector, context.Normal), 0.0) * M_1_PI;

        float shadow = 0.0f;
        if (Camera.HasSun)
        {
            float viewDist = -mul(Camera.ModelView, float4(context.Position, 1.0f)).z;
            shadow = Common.SampleSunShadowCSM(context.Position, context.Normal, viewDist);
        }
        // Preserve SoftwareModern's existing Lambert-only contract.
        directLighting.DiffuseDemodulated = Camera.SunColor.rgb * d * shadow;
    }

    public void DirectIlluminatePrimary(inout uint4 RandomSeed, int2 pixel, FBSDFContext context,
                                        inout FDirectLighting directLighting)
    {
        DirectIlluminate(RandomSeed, context, directLighting);
    }
}

// Sun-shadow policy: the only thing that differs between the hardware and software-tracing
// direct illuminators. Hardware casts a ray-query occlusion ray along the sampled sun
// direction; software tracing samples the CSM cascades. Everything else (light NEE, ReSTIR
// gather) is shared by FTracingDirectIlluminator below.
public interface ISunShadowPolicy
{
    // Sun visibility in [0,1] at the shading point for the sampled sun direction.
    float SunShadow(FBSDFContext context, float3 sunSampleDirection);
}

public struct FRayOcclusionSunShadow : ISunShadowPolicy
{
    FHardwareRayTracer tracer;
    public float SunShadow(FBSDFContext context, float3 sunSampleDirection)
    {
        return tracer.TraceOcclusion(context.Position, sunSampleDirection) ? 0.0f : 1.0f;
    }
}

public struct FCsmSunShadow : ISunShadowPolicy
{
    public float SunShadow(FBSDFContext context, float3 sunSampleDirection)
    {
        UniformBufferObject camera = Bindless.GetGpuscene().Camera[0];
        const float viewDistance = -mul(camera.ModelView, float4(context.Position, 1.0f)).z;
        return Common.SampleSunShadowCSM(context.Position, context.Normal, viewDistance);
    }
}

// BSDF-aware direct lighting shared by the hardware path tracer and software tracing. The
// visibility tracer (TTracer) and the sun-shadow policy (TSun) are the only degrees of
// freedom; sun NEE, finite/delta-light NEE and primary-surface ReSTIR gather are common.
// SoftwareTracing keeps the CSM sun while adding finite/delta-light NEE through finite
// voxel-DDA visibility. ReSTIR replaces only the primary Lambertian/Mixture diffuse light
// term; per-bounce DirectIlluminate keeps the classic single-sample estimator so SHARC
// accounting is unchanged.
public struct FTracingDirectIlluminator<TTracer : IRayTracer, TSun : ISunShadowPolicy>
    : IDirectIlluminator
{
    // Set on the primary view when ReSTIR is active (see DirectIlluminatePrimary).
    public bool RestirPrimary;
    // Primary-hit state for the ReSTIR temporal gates, set by the entry shader after
    // PrimaryHit. Must come from renderer state: reading the freshly written G-buffer back
    // inside the same invocation is not coherent.
    public uint PrimaryInstanceId;
    public float2 PrimaryMotionPixels;

    // Explicit __init: a bare local declaration does not reliably apply field initializers,
    // and an uninitialized RestirPrimary sent the SharcUpdate pass into the ReSTIR path with
    // a null parameter table (GPU page fault / device lost on some GPUs).
    public __init()
    {
        RestirPrimary = false;
        PrimaryInstanceId = 0;
        PrimaryMotionPixels = float2(0, 0);
    }

    TTracer tracer;
    TSun sunShadow;

    void SunIlluminate(inout uint4 randomSeed, FBSDFContext context,
                       inout FDirectLighting directLighting)
    {
        UniformBufferObject camera = Bindless.GetGpuscene().Camera[0];
        float3 lightVector = normalize(camera.SunDirection.xyz);
        float shadow = 0.0f;
        if (camera.HasSun)
        {
            lightVector = Common.SampleSunDirection(randomSeed, lightVector);
            shadow = sunShadow.SunShadow(context, lightVector);
        }
        Common.AccumulateSunBSDF(context, lightVector, shadow, camera.SunColor.rgb,
                                 directLighting);
    }

    void LightIlluminate(inout uint4 randomSeed, FBSDFContext context, bool includeDiffuse,
                         inout FDirectLighting directLighting)
    {
        GPUScene gpuScene = Bindless.GetGpuscene();
        UniformBufferObject camera = gpuScene.Camera[0];
        if (camera.LightCount == 0 || gpuScene.SoftMeshShader.Lights == 0)
        {
            return;
        }

        const uint lightIndex = Common.SelectLight(RandomFloat(randomSeed));
        LightObject light = gpuScene.Lights[lightIndex];
        const float lightPdf = asfloat(light.reserved2);
        if (lightPdf <= 0.0f || Common.GetLightSampleMeasure(light) <= 0.0f)
        {
            return;
        }

        const float2 uv = Common.CanonicalizeLightUV(light, RandomFloat2(randomSeed));
        FDirectLighting sampleDirect;
        float3 lightPosition;
        if (!Common.EvaluateLightDirectSample(lightIndex, uv, context, sampleDirect,
                                               lightPosition))
        {
            return;
        }

        const float3 shadowOrigin =
            tracer.GetAreaLightShadowOrigin(context.Position, context.Normal, lightPosition, EPS2);
        const float3 visibilityTarget =
            Common.GetLightVisibilityTarget(lightIndex, shadowOrigin, lightPosition);
        if (!tracer.TraceAreaLightSegment(shadowOrigin, visibilityTarget, EPS))
        {
            if (includeDiffuse)
            {
                directLighting.DiffuseDemodulated += sampleDirect.DiffuseDemodulated;
            }
            directLighting.SpecularRadiance += sampleDirect.SpecularRadiance;
        }
    }

    public void DirectIlluminate(inout uint4 randomSeed, FBSDFContext context,
                                 inout FDirectLighting directLighting)
    {
        SunIlluminate(randomSeed, context, directLighting);
        LightIlluminate(randomSeed, context, true, directLighting);
    }

    public void DirectIlluminatePrimary(inout uint4 randomSeed, int2 pixel, FBSDFContext context,
                                        inout FDirectLighting directLighting)
    {
        SunIlluminate(randomSeed, context, directLighting);
        // ReSTIR owns only material-aware diffuse. View-dependent specular stays in the
        // primary tracing pass and never enters the reservoir/cache.
        if (RestirPrimary && (context.MaterialModel == MaterialLambertian ||
                              context.MaterialModel == MaterialMixture))
        {
            // A non-delta Mixture still needs the light-sampled specular proposal paired with
            // EvaluateGlossyDirect; ReSTIR replaces only its diffuse light sampling.
            if (context.MaterialModel == MaterialMixture &&
                context.PerceptualRoughness > BSDF_DELTA_ROUGHNESS)
            {
                LightIlluminate(randomSeed, context, false, directLighting);
            }
            // Gather only: the light contribution is produced by the ReSTIR spatial / shade
            // pass (Core.RestirSpatialShade.comp.slang) and added to the diffuse channel there,
            // after this dispatch completes.
            RestirPrimaryGather(randomSeed, pixel, context,
                                PrimaryInstanceId, PrimaryMotionPixels, tracer);
        }
        else
        {
            LightIlluminate(randomSeed, context, true, directLighting);
        }
    }
}

// Hardware path tracer: ray-query sun occlusion + ray-query light visibility.
public typealias FHardwareDirectIlluminator =
    FTracingDirectIlluminator<FHardwareRayTracer, FRayOcclusionSunShadow>;

// Software tracing: CSM sun + finite voxel-DDA light visibility.
public typealias FSoftwareTracingDirectIlluminator =
    FTracingDirectIlluminator<FSoftwareRayTracer, FCsmSunShadow>;
