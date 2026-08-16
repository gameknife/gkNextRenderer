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
    // SelectLight, GetLightSampleMeasure and GetLightSelectionPdf live in LightGrid.slang: light
    // *selection* and its pdf are the grid's responsibility, and this file consumes them to decide how
    // much light arrives. Calls into them stay Common-qualified - unqualified lookup inside a
    // namespace does not reach declarations from another file of the same module.

    public float2 CanonicalizeLightUV(LightObject light, float2 uv)
    {
        return light.lightType == LightTypePoint ? float2(0, 0) : uv;
    }

    public float3 SampleLightPosition(LightObject light, float2 uv)
    {
        return light.lightType == LightTypePoint
            ? light.p0.xyz
            : light.p0.xyz + uv.x * (light.p1.xyz - light.p0.xyz) +
                             uv.y * (light.p3.xyz - light.p0.xyz);
    }

    // Forward renderers use one delta sample for point records and rectangular quadrature for
    // area records. Keep the type inspection in this module because LightObject's raw layout is
    // intentionally not exposed to shader entry points.
    public uint GetLightQuadratureSampleCount(uint lightIndex)
    {
        LightObject light = Bindless.GetGpuscene().Lights[lightIndex];
        return light.lightType == LightTypeArea ? 4u : 1u;
    }

    // Most-representative-point approximation for glossy reflection from a rectangle. Intersect
    // the perfect reflection ray with the emitter plane, then clamp that point to the p0/p1/p3
    // parallelogram. Evaluating GGX towards this point makes the highlight cover the continuous
    // projected emitter instead of exposing a small set of discrete quadrature directions.
    public float2 GetAreaLightSpecularUV(uint lightIndex, float3 position,
                                         float3 reflectionDirection)
    {
        LightObject light = Bindless.GetGpuscene().Lights[lightIndex];
        if (light.lightType != LightTypeArea)
        {
            return float2(0.5f);
        }

        const float3 edgeU = light.p1.xyz - light.p0.xyz;
        const float3 edgeV = light.p3.xyz - light.p0.xyz;
        const float3 lightNormal = normalize(light.normal_area.xyz);
        const float planeDenominator = dot(reflectionDirection, lightNormal);
        if (abs(planeDenominator) <= EPS)
        {
            return float2(0.5f);
        }

        const float planeDistance =
            dot(light.p0.xyz - position, lightNormal) / planeDenominator;
        if (planeDistance <= 0.0f)
        {
            return float2(0.5f);
        }

        const float3 onPlane = position + reflectionDirection * planeDistance - light.p0.xyz;
        const float uu = dot(edgeU, edgeU);
        const float uv = dot(edgeU, edgeV);
        const float vv = dot(edgeV, edgeV);
        const float determinant = uu * vv - uv * uv;
        if (determinant <= EPS * EPS)
        {
            return float2(0.5f);
        }

        const float du = dot(onPlane, edgeU);
        const float dv = dot(onPlane, edgeV);
        return saturate(float2((du * vv - dv * uv) / determinant,
                               (dv * uu - du * uv) / determinant));
    }

    // BSDF-independent evaluation of one finite light, for forward renderers that shade every
    // light deterministically instead of importance-sampling one of them. Returns the incident
    // direction and L_e * dOmega arriving at the shading point, so the caller applies its own
    // BRDF and receiver cosine. This is the same radiometry EvaluateLightDirectSample integrates
    // with the discrete selection PDF divided out, so summing it over all lights matches what the
    // path tracer converges to.
    public bool EvaluateLightIncident(uint lightIndex, float2 uv, float3 position,
                                      out float3 wi, out float3 incidentWeight,
                                      out float3 lightPosition)
    {
        wi = float3(0, 0, 1);
        incidentWeight = float3(0, 0, 0);
        lightPosition = float3(0, 0, 0);

        GPUScene gpuScene = Bindless.GetGpuscene();
        LightObject light = gpuScene.Lights[lightIndex];
        const float sampleMeasure = Common.GetLightSampleMeasure(light);
        if (sampleMeasure <= 0.0f)
        {
            return false;
        }
        Material lightMaterial = gpuScene.GetMaterial(light.lightMatIdx);
        if (lightMaterial.MaterialModel != MaterialDiffuseLight)
        {
            return false;
        }

        lightPosition = SampleLightPosition(light, CanonicalizeLightUV(light, uv));
        const float3 toLight = lightPosition - position;
        const float distanceSquared = dot(toLight, toLight);
        if (distanceSquared <= EPS * EPS)
        {
            return false;
        }
        wi = toLight * rsqrt(distanceSquared);

        float solidAngle;
        if (light.lightType == LightTypePoint)
        {
            // DiffuseLight RGB is radiant intensity for a point record, so the inverse-square
            // term alone converts it to irradiance per unit projected area.
            solidAngle = 1.0f / distanceSquared;
        }
        else
        {
            const float lightCosine = max(dot(normalize(light.normal_area.xyz), -wi), 0.0f);
            if (lightCosine <= 0.0f)
            {
                return false;
            }
            // A single centre sample stands in for the whole emitter. Clamping to the hemisphere
            // keeps a receiver that sits almost on a large quad from producing an unbounded
            // highlight, which a deterministic forward pass has no filtering to hide.
            solidAngle = min(sampleMeasure * lightCosine / distanceSquared, M_TWO_PI);
        }
        incidentWeight = lightMaterial.Diffuse.rgb * solidAngle;
        return any(incidentWeight > float3(0, 0, 0));
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
    // Entry point for callers that just selected this light and therefore already hold its
    // selection probability. Re-deriving it would mean a second light-grid lookup per sample, and
    // SelectLightAt returns exactly what GetLightSelectionPdf reports for the same position and
    // light, so passing it through is the same number by construction rather than a second opinion.
    public bool EvaluateLightDirectSampleWithPdf(uint lightIndex, float lightPdf, float2 uv,
                                                 FBSDFContext context, out FDirectLighting direct,
                                                 out float3 lightPosition)
    {
        direct = FDirectLighting();
        lightPosition = float3(0, 0, 0);
        GPUScene gpuScene = Bindless.GetGpuscene();
        LightObject light = gpuScene.Lights[lightIndex];
        const float sampleMeasure = Common.GetLightSampleMeasure(light);
        if (sampleMeasure <= 0.0f || lightPdf <= 0.0f)
        {
            return false;
        }
        Material lightMaterial = gpuScene.GetMaterial(light.lightMatIdx);
        if (lightMaterial.MaterialModel != MaterialDiffuseLight)
        {
            return false;
        }

        lightPosition = SampleLightPosition(light, uv);
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

    // Entry point for callers holding only a light index - a stored ReSTIR reservoir, or a BSDF ray
    // that landed on an emitter. Resolves the selection probability at the shading point itself.
    public bool EvaluateLightDirectSample(uint lightIndex, float2 uv, FBSDFContext context,
                                          out FDirectLighting direct,
                                          out float3 lightPosition)
    {
        return EvaluateLightDirectSampleWithPdf(
            lightIndex, Common.GetLightSelectionPdf(lightIndex, context.Position), uv, context,
            direct, lightPosition);
    }

    // ReSTIR target/shading payload: BSDF-aware diffuse only, excluding the full
    // source PDF (discrete light probability divided by finite sample measure).
    public bool EvaluateLightDiffuseSampleWithPdf(uint lightIndex, float lightPdf, float2 uv,
                                                  FBSDFContext context, out float3 f,
                                                  out float3 lightPosition)
    {
        f = float3(0, 0, 0);
        FDirectLighting direct;
        if (!EvaluateLightDirectSampleWithPdf(lightIndex, lightPdf, uv, context, direct,
                                              lightPosition))
        {
            return false;
        }
        LightObject light = Bindless.GetGpuscene().Lights[lightIndex];
        const float sampleMeasure = Common.GetLightSampleMeasure(light);
        if (sampleMeasure <= 0.0f)
        {
            return false;
        }
        // Cancels the pdf the evaluator divided out, so the target function stays a pure
        // property of the surface and the light - identical whichever pixel's cell produced the
        // sample, which is what makes a reservoir safe to reuse across pixels.
        f = direct.DiffuseDemodulated * (lightPdf / sampleMeasure);
        return any(f > float3(0, 0, 0));
    }

    public bool EvaluateLightDiffuseSample(uint lightIndex, float2 uv, FBSDFContext context,
                                           out float3 f, out float3 lightPosition)
    {
        return EvaluateLightDiffuseSampleWithPdf(
            lightIndex, Common.GetLightSelectionPdf(lightIndex, context.Position), uv, context, f,
            lightPosition);
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

    // CSM disk kernel 的随机旋转角：world position 白噪声 hash + 每帧 golden-ratio 去相关
    // （与 SceneSampling.ScreenSpaceHash 相同的时域序列）。逐点独立、逐帧变化，是时域
    // upscaler 把旋转 kernel 平均回均匀圆盘所需要的那种"快"噪声。
    float SunShadowJitterRotation(float3 worldPos, uint frame)
    {
        uint seed = (asuint(worldPos.x) * 0x9E3779B9u) ^ (asuint(worldPos.y) * 0x85EBCA6Bu) ^
                    (asuint(worldPos.z) * 0xC2B2AE35u);
        seed ^= seed >> 16u;
        seed *= 0x7FEB352Du;
        seed ^= seed >> 15u;
        seed *= 0x846CA68Bu;
        seed ^= seed >> 16u;
        return frac(float(seed & 0x00FFFFFFu) / 16777216.0f + float(frame) * 0.6180339887f);
    }

    // 8-tap Vogel disk + slope-scaled bias，对 4 个 CSM cascade 都通用。
    // 返回 [0,1]，1 = 完全无遮蔽，0 = 完全阴影。
    // 时域超分激活（PrimaryRayJitter）时 disk kernel 逐帧随机旋转：单帧是有颗粒感的
    // 随机采样，upscaler 的时域累积把多帧旋转 kernel 平均成均匀圆盘，得到比固定 3x3 PCF
    // 更软、无带状伪影的半影；无时域累积时旋转角固定为 0，退化为确定性 disk PCF。
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

        const float rotation = Camera.PrimaryRayJitter
            ? Common.SunShadowJitterRotation(worldPos, Camera.TotalFrames)
            : 0.0f;

        float shadow = 0.0f;
        [unroll]
        for (uint tap = 0u; tap < 8u; ++tap)
        {
            // Vogel disk：golden-angle 螺旋均匀覆盖圆盘，旋转副本在时域平均下收敛到
            // 整个均匀圆盘。半径 2 texel，比 3x3 方形核（角点 ~1.4 texel）略宽以获得
            // 更软的半影。
            const float radius = sqrt((float(tap) + 0.5f) * 0.125f);
            const float theta = float(tap) * 2.39996323f + rotation * M_TWO_PI;
            float2 offset = (2.0f * radius) * texelSize * float2(cos(theta), sin(theta));
            float closestDepth = shadowMap.SampleLevel(uv + offset, 0).r;
            shadow += (projCoords.z - bias) > closestDepth ? 0.0f : 1.0f;
        }
        return shadow * 0.125f;
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

        uint lightIndex;
        float lightPdf;
        if (!Common.SelectLightAt(context.Position, RandomFloat(randomSeed), lightIndex, lightPdf))
        {
            return;
        }
        LightObject light = gpuScene.Lights[lightIndex];
        if (Common.GetLightSampleMeasure(light) <= 0.0f)
        {
            return;
        }

        const float2 uv = Common.CanonicalizeLightUV(light, RandomFloat2(randomSeed));
        FDirectLighting sampleDirect;
        float3 lightPosition;
        if (!Common.EvaluateLightDirectSampleWithPdf(lightIndex, lightPdf, uv, context,
                                                      sampleDirect, lightPosition))
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
