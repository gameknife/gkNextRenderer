implementing PathTracing;

#include "PreProcessor.slang"

// Direct illumination strategies + path-tracing renderer split out of Shading.slang.
// Houses:
//   - FHardwareDirectIlluminator   : sun shadow via RayQuery
//   - FAmbientCubeIlluminator      : null illuminator (probe-only path)
//   - FSoftwareDirectIlluminator   : cascade-aware DDA shadow
//   - FShadowMapDirectIlluminator  : shadow-map sampling
//   - FPathTracingRenderer         : multi-bounce path tracer driving any IRayTracer + IDirectIlluminator
// IRayTracer impls live in RayTracers.slang; primary ray casters and interfaces stay in Shading.slang.

namespace Common
{
    // CDF light selection shared by classic area-light NEE and ReSTIR candidate generation
    // (reserved1 carries the cdf built in Scene::UpdateLights).
    public uint SelectAreaLight(float lightSelector)
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

    // BSDF-aware area-light sample. Diffuse stays albedo-demodulated while specular is
    // complete radiance; lightPdf is the discrete light-selection probability.
    public bool EvaluateAreaLightDirectSample(uint lightIndex, float2 uv, FBSDFContext context,
                                              out FDirectLighting direct,
                                              out float3 lightPosition)
    {
        direct = FDirectLighting();
        lightPosition = float3(0, 0, 0);
        GPUScene gpuScene = Bindless.GetGpuscene();
        LightObject light = gpuScene.Lights[lightIndex];
        const float lightArea = light.normal_area.w;
        const float lightPdf = asfloat(light.reserved2);
        if (lightArea <= 0.0f || lightPdf <= 0.0f)
        {
            return false;
        }
        Material lightMaterial = gpuScene.GetMaterial(light.lightMatIdx);
        if (lightMaterial.MaterialModel != MaterialDiffuseLight)
        {
            return false;
        }

        lightPosition = light.p0.xyz + uv.x * (light.p1.xyz - light.p0.xyz) +
                        uv.y * (light.p3.xyz - light.p0.xyz);
        const float3 toLight = lightPosition - context.Position;
        const float distanceSquared = dot(toLight, toLight);
        if (distanceSquared <= EPS * EPS)
        {
            return false;
        }
        const float3 wi = toLight * rsqrt(distanceSquared);
        const float surfaceCosine = max(dot(context.Normal, wi), 0.0f);
        const float lightCosine = max(dot(normalize(light.normal_area.xyz), -wi), 0.0f);
        if (surfaceCosine <= 0.0f || lightCosine <= 0.0f)
        {
            return false;
        }

        FBSDFEval bsdf = EvaluateSurfaceBSDF(context, wi);
        const float lightPdfOmega = (lightPdf / lightArea) * distanceSquared / lightCosine;
        const float specularMis = PowerHeuristic(
            lightPdfOmega, EvaluateGlossyProposalPdf(context, wi));
        const float geometryAndPdf = surfaceCosine * lightCosine * lightArea /
                                     (distanceSquared * lightPdf);
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

    // ReSTIR target/shading payload: BSDF-aware diffuse only, excluding source PDF.
    public bool EvaluateAreaLightDiffuseSample(uint lightIndex, float2 uv, FBSDFContext context,
                                               out float3 f, out float3 lightPosition)
    {
        f = float3(0, 0, 0);
        FDirectLighting direct;
        if (!EvaluateAreaLightDirectSample(lightIndex, uv, context, direct, lightPosition))
        {
            return false;
        }
        LightObject light = Bindless.GetGpuscene().Lights[lightIndex];
        const float lightArea = light.normal_area.w;
        const float lightPdf = asfloat(light.reserved2);
        if (lightArea <= 0.0f || lightPdf <= 0.0f)
        {
            return false;
        }
        f = direct.DiffuseDemodulated * (lightPdf / lightArea);
        return any(f > float3(0, 0, 0));
    }

    public float3 SampleSunDirection(inout uint4 randomSeed, float3 sunDirection)
    {
        const float cosRadius = cos(0.25f / 180.0f * M_PI);
        return AlignWithNormal(RandomInCone(randomSeed, cosRadius), normalize(sunDirection));
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
            const float cosRadius = cos(0.25f / 180.0f * M_PI);
            const float sunPdf = 1.0f / (M_TWO_PI * max(1.0f - cosRadius, 1e-7f));
            const float specularMis = PowerHeuristic(
                sunPdf, EvaluateGlossyProposalPdf(context, wi));
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
        const float cosRadius = cos(0.25f / 180.0f * M_PI);
        if (dot(normalize(rayDirection), normalize(camera.SunDirection.xyz)) < cosRadius)
        {
            return float3(0, 0, 0);
        }
        const float solidAngle = M_TWO_PI * max(1.0f - cosRadius, 1e-7f);
        return camera.SunColor.rgb / solidAngle;
    }
}

public struct FHardwareDirectIlluminator : IDirectIlluminator
{
    // Set on the primary view when ReSTIR is active: the primary-surface area-light term is
    // then produced by reservoir resampling. Per-bounce DirectIlluminate keeps the classic
    // single-sample estimator so SHARC cache accounting is unchanged.
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

    void SunIlluminate(inout uint4 RandomSeed, FBSDFContext context,
                       inout FDirectLighting directLighting)
    {
        UniformBufferObject Camera = Bindless.GetGpuscene().Camera[0];
        float shadow = 0.0f;
        float3 lightVector = normalize(Camera.SunDirection.xyz);
        if (Camera.HasSun)
        {
            lightVector = Common.SampleSunDirection(RandomSeed, lightVector);
            shadow = 1;
            if (HWTracer.TraceOcclusion(context.Position, lightVector))
            {
                shadow = 0;
            }
        }
        Common.AccumulateSunBSDF(context, lightVector, shadow, Camera.SunColor.rgb,
                                 directLighting);
    }

    void AreaIlluminate(inout uint4 RandomSeed, FBSDFContext context, bool includeDiffuse,
                        inout FDirectLighting directLighting)
    {
        GPUScene gpuScene = Bindless.GetGpuscene();
        UniformBufferObject Camera = gpuScene.Camera[0];
        if (Camera.LightCount == 0 || gpuScene.SoftMeshShader.Lights == 0)
        {
            return;
        }

        const uint lightIndex = Common.SelectAreaLight(RandomFloat(RandomSeed));
        LightObject light = gpuScene.Lights[lightIndex];
        const float lightPdf = asfloat(light.reserved2);
        const float lightArea = light.normal_area.w;
        if (lightPdf <= 0.0f || lightArea <= 0.0f)
        {
            return;
        }

        const float2 uv = RandomFloat2(RandomSeed);
        FDirectLighting sampleDirect;
        float3 lightPosition;
        if (!Common.EvaluateAreaLightDirectSample(lightIndex, uv, context, sampleDirect,
                                                   lightPosition))
        {
            return;
        }

        const float3 shadowOrigin =
            HWTracer.GetAreaLightShadowOrigin(context.Position, context.Normal, lightPosition, EPS2);
        if (!HWTracer.TraceAreaLightSegment(shadowOrigin, lightPosition, EPS))
        {
            if (includeDiffuse)
            {
                directLighting.DiffuseDemodulated += sampleDirect.DiffuseDemodulated;
            }
            directLighting.SpecularRadiance += sampleDirect.SpecularRadiance;
        }
    }

    public void DirectIlluminate(inout uint4 RandomSeed, FBSDFContext context,
                                 inout FDirectLighting directLighting)
    {
        SunIlluminate(RandomSeed, context, directLighting);
        AreaIlluminate(RandomSeed, context, true, directLighting);
    }

    public void DirectIlluminatePrimary(inout uint4 RandomSeed, int2 pixel, FBSDFContext context,
                                        inout FDirectLighting directLighting)
    {
        SunIlluminate(RandomSeed, context, directLighting);
        // ReSTIR owns only material-aware diffuse. View-dependent specular stays in the
        // primary tracing pass and never enters the reservoir/cache.
        if (RestirPrimary && (context.MaterialModel == MaterialLambertian ||
                              context.MaterialModel == MaterialMixture))
        {
            // ReSTIR replaces only diffuse area-light sampling. A non-delta Mixture still
            // needs the light-sampled specular proposal paired with EvaluateGlossyDirect.
            if (context.MaterialModel == MaterialMixture &&
                context.PerceptualRoughness > BSDF_DELTA_ROUGHNESS)
            {
                AreaIlluminate(RandomSeed, context, false, directLighting);
            }
            // Gather only: the area-light contribution is produced by the ReSTIR spatial /
            // shade pass (Core.RestirSpatialShade.comp.slang) and added to the diffuse
            // channel there, after this dispatch completes.
            RestirPrimaryGather(RandomSeed, pixel, context,
                                PrimaryInstanceId, PrimaryMotionPixels, HWTracer);
        }
        else
        {
            AreaIlluminate(RandomSeed, context, true, directLighting);
        }
    }

    FHardwareRayTracer HWTracer;
}

public struct FAmbientCubeIlluminator : IDirectIlluminator
{
    public void DirectIlluminate(inout uint4 RandomSeed, FBSDFContext context,
                                 inout FDirectLighting directLighting)
    {
        directLighting = FDirectLighting();
    }

    public void DirectIlluminatePrimary(inout uint4 RandomSeed, int2 pixel, FBSDFContext context,
                                        inout FDirectLighting directLighting)
    {
        DirectIlluminate(RandomSeed, context, directLighting);
    }
}

public struct FSoftwareDirectIlluminator : IDirectIlluminator
{
    public void DirectIlluminate(inout uint4 RandomSeed, FBSDFContext context,
                                 inout FDirectLighting directLighting)
    {
        UniformBufferObject Camera = Bindless.GetGpuscene().Camera[0];
        float shadow = 0.0f;
        float3 lightVector = normalize(Camera.SunDirection.xyz);
        if (Camera.HasSun)
        {
            const uint cascadeIndex = GetAmbientCubeCascadeAtPosition(context.Position);
            const float cascadeUnit = GetAmbientCubeUnitAtCascade(cascadeIndex);
            lightVector = Common.SampleSunDirection(RandomSeed, lightVector);
            shadow = 1;
            float3 normalDir = context.Normal;
            float normalOffset = max(cascadeUnit * 0.5f, 0.25f);
            float rayOffset = cascadeUnit * 2.5f;
            if (cascadeIndex > 0)
            {
                normalOffset = cascadeUnit * (1.0f + 0.5f * float(cascadeIndex));
                rayOffset = cascadeUnit * (3.0f + float(cascadeIndex));
            }
            float3 shadowOrigin = context.Position + normalDir * normalOffset + lightVector * rayOffset;
            if (HWTracer.TraceOcclusion(shadowOrigin, lightVector))
            {
                shadow = 0;
            }
        }
        Common.AccumulateSunBSDF(context, lightVector, shadow, Camera.SunColor.rgb,
                                 directLighting);
    }

    public void DirectIlluminatePrimary(inout uint4 RandomSeed, int2 pixel, FBSDFContext context,
                                        inout FDirectLighting directLighting)
    {
        DirectIlluminate(RandomSeed, context, directLighting);
    }

    // all stuffs like pointers
    FSoftwareRayTracer HWTracer;
}

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

// SoftwareTracing direct lighting keeps the existing CSM sun while adding area-light NEE
// through finite voxel-DDA visibility. It is intentionally separate from
// FShadowMapDirectIlluminator so SoftwareModern keeps its current sun-only contract.
public struct FSoftwareTracingDirectIlluminator : IDirectIlluminator
{
    public bool RestirPrimary;
    public uint PrimaryInstanceId;
    public float2 PrimaryMotionPixels;

    public __init()
    {
        RestirPrimary = false;
        PrimaryInstanceId = 0;
        PrimaryMotionPixels = float2(0, 0);
    }

    void SunIlluminate(inout uint4 randomSeed, FBSDFContext context,
                       inout FDirectLighting directLighting)
    {
        UniformBufferObject camera = Bindless.GetGpuscene().Camera[0];
        float3 lightVector = normalize(camera.SunDirection.xyz);
        float shadow = 0.0f;
        if (camera.HasSun)
        {
            lightVector = Common.SampleSunDirection(randomSeed, lightVector);
            const float viewDistance = -mul(camera.ModelView, float4(context.Position, 1.0f)).z;
            shadow = Common.SampleSunShadowCSM(context.Position, context.Normal, viewDistance);
        }
        Common.AccumulateSunBSDF(context, lightVector, shadow, camera.SunColor.rgb,
                                 directLighting);
    }

    void AreaIlluminate(inout uint4 randomSeed, FBSDFContext context, bool includeDiffuse,
                        inout FDirectLighting directLighting)
    {
        GPUScene gpuScene = Bindless.GetGpuscene();
        UniformBufferObject camera = gpuScene.Camera[0];
        if (camera.LightCount == 0 || gpuScene.SoftMeshShader.Lights == 0)
        {
            return;
        }

        const uint lightIndex = Common.SelectAreaLight(RandomFloat(randomSeed));
        LightObject light = gpuScene.Lights[lightIndex];
        const float lightPdf = asfloat(light.reserved2);
        const float lightArea = light.normal_area.w;
        if (lightPdf <= 0.0f || lightArea <= 0.0f)
        {
            return;
        }

        const float2 uv = RandomFloat2(randomSeed);
        FDirectLighting sampleDirect;
        float3 lightPosition;
        if (!Common.EvaluateAreaLightDirectSample(lightIndex, uv, context, sampleDirect,
                                                   lightPosition))
        {
            return;
        }

        const float3 shadowOrigin =
            SoftwareTracer.GetAreaLightShadowOrigin(context.Position, context.Normal,
                                                    lightPosition, EPS2);
        if (!SoftwareTracer.TraceAreaLightSegment(shadowOrigin, lightPosition, EPS))
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
        AreaIlluminate(randomSeed, context, true, directLighting);
    }

    public void DirectIlluminatePrimary(inout uint4 randomSeed, int2 pixel, FBSDFContext context,
                                        inout FDirectLighting directLighting)
    {
        SunIlluminate(randomSeed, context, directLighting);
        if (RestirPrimary && (context.MaterialModel == MaterialLambertian ||
                              context.MaterialModel == MaterialMixture))
        {
            if (context.MaterialModel == MaterialMixture &&
                context.PerceptualRoughness > BSDF_DELTA_ROUGHNESS)
            {
                AreaIlluminate(randomSeed, context, false, directLighting);
            }
            RestirPrimaryGather(randomSeed, pixel, context,
                                PrimaryInstanceId, PrimaryMotionPixels, SoftwareTracer);
        }
        else
        {
            AreaIlluminate(randomSeed, context, true, directLighting);
        }
    }

    FSoftwareRayTracer SoftwareTracer;
}

public struct FPathTracingRenderer{
    public float ExitProbability = 0.5;
    public bool ExitAfterFirst = false;
    public float HitNormalOffset = 0.0f;
    public int SampleDownscale = 1;
    public bool FuzzyTracing = false;
    public bool ForceExitAfterFirst = false;
    public bool EnableSoftTracingAdaptiveOffset = false;
    public bool UseAmbientCubeTerminal = true;
    public bool WriteOutputs = true;
    // Secondary (post-first-bounce) direct lighting strategy, decoupled from any cache:
    //   0 = PlainSunNEE          : naive PT 50% random sun next-event estimation
    //   1 = DirectTerminateIfLit : terminate the path once it sees direct light (cache provides indirect)
    //   2 = None                 : no secondary direct lighting (direct already fed to the cache)
    public uint SecondaryDirectMode = 0;

    [mutating]
    public FDirectLighting EvaluateDirectLighting(IDirectIlluminator directIllum, Vertex vertex,
                                                  float3 incomingRayDirection)
    {
        Material material = Bindless.GetGpuscene().GetMaterial(vertex.MaterialIndex);
        FBSDFContext context = BuildBSDFContext(vertex, material, incomingRayDirection);
        FDirectLighting direct = FDirectLighting();
        directIllum.DirectIlluminate(RandomSeed_, context, direct);
        direct.DiffuseDemodulated = max(direct.DiffuseDemodulated, float3(0, 0, 0));
        direct.SpecularRadiance = max(direct.SpecularRadiance, float3(0, 0, 0));
        return direct;
    }

    // Radiance applied when a path is forced to terminate on a surface.
    // Returns the terminal radiance; the caller multiplies it into RayColor and,
    // when ambient-cube terminal is active, feeds it to the cache as a miss.
    float4 ApplyTerminalRadiance(Vertex vertex)
    {
        if (UseAmbientCubeTerminal)
        {
            return interpolateAmbientCubes<FullAmbientCubeSampler>(vertex.Position, vertex.Normal);
        }
        return float4(0, 0, 0, 1);
    }

    bool IsRegisteredAreaLightHit(float3 position, uint materialIndex)
    {
        GPUScene gpuScene = Bindless.GetGpuscene();
        UniformBufferObject Camera = gpuScene.Camera[0];
        for (uint i = 0; i < Camera.LightCount; ++i)
        {
            LightObject light = gpuScene.Lights[i];
            if (light.lightMatIdx != materialIndex)
            {
                continue;
            }

            if (IsPointOnAreaLight(position, light, EPS10))
            {
                return true;
            }
        }
        return false;
    }

    bool GetRegisteredAreaLightPdf(float3 position, uint materialIndex, float3 shadingPosition,
                                   out float pdfOmega)
    {
        pdfOmega = 0.0f;
        GPUScene gpuScene = Bindless.GetGpuscene();
        UniformBufferObject camera = gpuScene.Camera[0];
        for (uint i = 0; i < camera.LightCount; ++i)
        {
            LightObject light = gpuScene.Lights[i];
            if (light.lightMatIdx != materialIndex ||
                !IsPointOnAreaLight(position, light, EPS10))
            {
                continue;
            }
            const float3 toLight = position - shadingPosition;
            const float distanceSquared = dot(toLight, toLight);
            const float lightCosine = max(dot(normalize(light.normal_area.xyz),
                                               -normalize(toLight)), 0.0f);
            const float area = light.normal_area.w;
            const float lightSelectionPdf = asfloat(light.reserved2);
            if (distanceSquared > EPS * EPS && lightCosine > 0.0f && area > 0.0f &&
                lightSelectionPdf > 0.0f)
            {
                pdfOmega = (lightSelectionPdf / area) * distanceSquared / lightCosine;
                return true;
            }
        }
        return false;
    }

    float3 FetchEmitterRadiance(Vertex vertex)
    {
        Material material = Bindless.GetGpuscene().GetMaterial(vertex.MaterialIndex);
        if (material.MaterialModel != MaterialDiffuseLight)
        {
            return float3(0, 0, 0);
        }
        float3 radiance = material.Diffuse.rgb;
        if (material.DiffuseTextureId >= 0)
        {
            radiance *= Bindless.GetSampleTexture(material.DiffuseTextureId)
                                .SampleLevel(vertex.TexCoord, 0).rgb;
        }
        return radiance;
    }

    // Glossy direct-light proposal. Near-delta reflection is deterministic; rough GGX uses
    // a normalized proposal with MIS against area/sun light sampling.
    float3 EvaluateGlossyDirect(IRayTracer tracer, FBSDFContext context, Vertex sourceVertex,
                                float normalOffset, inout uint4 randomSeed)
    {
        if (context.MaterialModel != MaterialMixture &&
            context.MaterialModel != MaterialMetallic &&
            context.MaterialModel != MaterialDielectric)
        {
            return float3(0, 0, 0);
        }

        const float3 incoming = -context.Wo;
        const float NoV = max(dot(context.Normal, context.Wo), 0.0f);
        const float3 fresnel = BSDFFresnelSchlick(NoV, BSDFMaterialF0(context));
        float3 result = float3(0, 0, 0);

        const bool isDelta = context.PerceptualRoughness <= BSDF_DELTA_ROUGHNESS;
        float proposalPdf = 0.0f;
        const float3 reflected = isDelta
            ? normalize(reflect(incoming, context.Normal))
            : SampleGlossyProposal(randomSeed, context, proposalPdf);
        if (dot(reflected, context.Normal) <= 0.0f)
        {
            return float3(0, 0, 0);
        }
        Vertex reflectedHit = sourceVertex;
        NodeProxy reflectedNode;
        float4 traceColor = float4(1, 1, 1, 1);
        float hitDistance = 0.0f;
        const float3 reflectionOrigin = context.Position + context.Normal * max(normalOffset, EPS2);
        if (tracer.TraceRay(reflectionOrigin, reflected, PT_MAX_TRACE_DISTANCE, reflectedHit,
                            reflectedNode, traceColor, hitDistance))
        {
            float lightPdf = 0.0f;
            if (GetRegisteredAreaLightPdf(reflectedHit.Position, reflectedHit.MaterialIndex,
                                          context.Position, lightPdf))
            {
                if (isDelta)
                {
                    result += fresnel * FetchEmitterRadiance(reflectedHit);
                }
                else if (proposalPdf > 0.0f)
                {
                    FBSDFEval bsdf = EvaluateSurfaceBSDF(context, reflected);
                    const float NoL = max(dot(context.Normal, reflected), 0.0f);
                    const float mis = PowerHeuristic(proposalPdf, lightPdf);
                    result += FetchEmitterRadiance(reflectedHit) * bsdf.Specular *
                              (NoL * mis / proposalPdf);
                }
            }
        }
        else
        {
            const float3 sunRadiance = Common.EvaluateAnalyticSunDisk(reflected);
            if (isDelta)
            {
                result += fresnel * sunRadiance;
            }
            else if (proposalPdf > 0.0f && any(sunRadiance > float3(0, 0, 0)))
            {
                const float cosRadius = cos(0.25f / 180.0f * M_PI);
                const float sunPdf = 1.0f /
                    (M_TWO_PI * max(1.0f - cosRadius, 1e-7f));
                FBSDFEval bsdf = EvaluateSurfaceBSDF(context, reflected);
                const float NoL = max(dot(context.Normal, reflected), 0.0f);
                const float mis = PowerHeuristic(proposalPdf, sunPdf);
                result += sunRadiance * bsdf.Specular * (NoL * mis / proposalPdf);
            }
        }

        if (isDelta && context.MaterialModel == MaterialDielectric)
        {
            const float eta = context.FrontFace
                ? context.EtaOutside / context.EtaInside
                : context.EtaInside / context.EtaOutside;
            const float3 transmitted = refract(incoming, context.Normal, eta);
            if (dot(transmitted, transmitted) > 1e-8f)
            {
                Vertex transmittedHit = sourceVertex;
                NodeProxy transmittedNode;
                traceColor = float4(1, 1, 1, 1);
                hitDistance = 0.0f;
                const float3 transmissionOrigin =
                    context.Position - context.Normal * max(normalOffset, EPS2);
                if (tracer.TraceRay(transmissionOrigin, normalize(transmitted), PT_MAX_TRACE_DISTANCE,
                                    transmittedHit, transmittedNode, traceColor, hitDistance))
                {
                    if (IsRegisteredAreaLightHit(transmittedHit.Position,
                                                 transmittedHit.MaterialIndex))
                    {
                        result += (1.0f - fresnel) * FetchEmitterRadiance(transmittedHit);
                    }
                }
                else
                {
                    result += (1.0f - fresnel) *
                              Common.EvaluateAnalyticSunDisk(normalize(transmitted));
                }
            }
        }
        return result;
    }

    bool GetRayColor(IRayTracer tracer, inout Vertex inVertex, inout float3 rayDir, inout float4 rayColor,
                     inout uint4 RandomSeed, bool suppressDiffuseEmitter,
                     bool suppressSpecularEmitter, inout bool hitReflect, inout bool hitMetal,
                     out float outHitDist, out bool outHitMiss,
                     out float3 outTerminalRadiance)
    {
        UniformBufferObject Camera = Bindless.GetGpuscene().Camera[0];
        outHitMiss = false;
        outTerminalRadiance = float3(0, 0, 0);

        Material mat = Bindless.GetGpuscene().GetMaterial(inVertex.MaterialIndex);
        const float startPosOffset = mat.MaterialModel == MaterialDielectric ? 0.0f : 1.0f;
        float roughness = mat.Fuzziness;
        float metalness = mat.Metalness;

        if (mat.MRATextureId >= 0)
        {
            float4 mra = Bindless.GetSampleTexture(mat.MRATextureId).SampleLevel(inVertex.TexCoord, 0);
            roughness = roughness * mra.g;
            metalness = metalness * mra.b;
        }

        const float dotValue = dot(rayDir, inVertex.Normal);
        const bool BackFace = dotValue > 0;
        const float3 outwardNormal = BackFace ? -inVertex.Normal : inVertex.Normal;
        const float niOverNt = BackFace ? mat.RefractionIndex2 : (1 / mat.RefractionIndex2);
        const float cosine = dotValue > 0 ? mat.RefractionIndex * dotValue : -dotValue;
        const float reflectProb = Schlick(cosine, mat.RefractionIndex);// * pow((1.0 - roughness), 4.0);
        const float cosTheta = min(dot(-rayDir, outwardNormal), 1.0f);
        const float sinTheta = sqrt(max(0.0f, 1.0f - cosTheta * cosTheta));
        const bool cannotRefract = niOverNt * sinTheta > 1.0f;
        const float metalProb = metalness;

        bool chanceReflect = (RandomFloat(RandomSeed) < reflectProb);
        bool chanceMetal = (RandomFloat(RandomSeed) < metalProb);
        bool chanceGGX = chanceReflect || chanceMetal;
        const float3 trace_next = chanceGGX ? reflect(rayDir, outwardNormal) : outwardNormal;
        float3 trace_dir = chanceGGX ? ggxSampling(RandomSeed, sqrt(roughness), trace_next) : AlignWithNormal(RandomInHemiSphere1(RandomSeed), trace_next);

        hitReflect = chanceGGX;
        hitMetal = chanceMetal;

        if (mat.MaterialModel == MaterialDielectric)
        {
            if (!chanceReflect)
            {
                if (cannotRefract)
                {
                    trace_dir = ggxSampling(RandomSeed, sqrt(roughness), reflect(rayDir, outwardNormal));
                    hitReflect = true;
                }
                else
                {
                    trace_dir = refract(rayDir, outwardNormal, niOverNt);
                }
            }
        }

        rayDir = trace_dir;

        NodeProxy hitNode;
        float4 tempColor = float4(0, 0, 0, 1);
        float3 cachePosition = inVertex.Position;
        float3 cacheNormal = inVertex.Normal;
        float surfaceOffset = HitNormalOffset;
        if (EnableSoftTracingAdaptiveOffset)
        {
            surfaceOffset = max(HitNormalOffset, GetAmbientCubeUnitAtPosition(inVertex.Position) * 0.5f);
        }
        const float3 traceOrigin = inVertex.Position + inVertex.Normal * surfaceOffset * startPosOffset;
        if (tracer.TraceRay(traceOrigin, trace_dir, PT_MAX_TRACE_DISTANCE, inVertex, hitNode, tempColor, outHitDist))
        {
            if(FuzzyTracing)
            {
                rayColor *= tempColor;
                return false;
            }
            Material hitMat = Bindless.GetGpuscene().GetMaterial(inVertex.MaterialIndex);
            float4 outAlbedo = hitMat.Diffuse;
            if (hitMat.DiffuseTextureId >= 0)
            {
                float4 tex = Bindless.GetSampleTexture(hitMat.DiffuseTextureId).SampleLevel(inVertex.TexCoord, 0);
                outAlbedo *= tex;
            }
            if (hitMat.MaterialModel == MaterialDiffuseLight || !chanceReflect)
            {
                rayColor *= outAlbedo;
            }
            if (BackFace && mat.MaterialModel != MaterialDielectric)
            {
                rayColor = float4(0.5f,0.5f,0.5f,0);
                return true;
            }
            if (hitMat.MaterialModel == MaterialDiffuseLight)
            {
                const bool dielectricTransmission =
                    mat.MaterialModel == MaterialDielectric && !chanceGGX;
                const bool coveredByDirectEstimator = chanceGGX
                    ? suppressSpecularEmitter
                    : (suppressDiffuseEmitter && !dielectricTransmission);
                if (coveredByDirectEstimator &&
                    IsRegisteredAreaLightHit(inVertex.Position, inVertex.MaterialIndex))
                {
                    // The same primary diffuse-light path is already represented by area-light NEE.
                    rayColor = float4(0, 0, 0, 0);
                    return true;
                }
                outTerminalRadiance = outAlbedo.rgb;
                return true;
            }
            return false;
        }
        else
        {
            // if(FuzzyTracing)
            // {
            //     float4 skyColor = Camera.HasSky ? Common.SampleIBLRough(Camera.SkyIdx, trace_dir, Camera.SkyRotation) * Camera.SkyIntensity : float4(0.0, 0.0, 0.0, 0.0);
            //     rayColor *= skyColor;
            // }
            // else
            {
                float4 skyColor = Camera.HasSky ? Common.SampleIBL(Camera.SkyIdx, trace_dir, Camera.SkyRotation, 0) * Camera.SkyIntensity : float4(0.0, 0.0, 0.0, 0.0);
                rayColor *= skyColor;
                outHitMiss = true;
                outTerminalRadiance = skyColor.rgb;
            }
            outHitDist = PT_MAX_TRACE_DISTANCE;
            return true;
        }
    }

    [mutating]
    public void Render<TCache : IRadianceCache>(IRayTracer tracer, IDirectIlluminator directIllum, inout TCache cache, int sampleMultiplier)
    {
        UniformBufferObject Camera = Bindless.GetGpuscene().Camera[0];

        if (!cache.ShouldShadePixel(pixel_, Camera))
        {
            return;
        }

        float4 FinalColor = float4(0, 0, 0, 0);
        float4 FinalReflection = float4(0, 0, 0, 0);
        float FinalDiffuseHitDist = 0;
        float FinalSpecularHitDist = 0;

        Material mat = Bindless.GetGpuscene().GetMaterial(hitPrimaryVertex_.MaterialIndex);

        const int sample = max(1, Camera.NumberOfSamples / SampleDownscale * sampleMultiplier);
        const bool wantsDirect = cache.WantsDirectLighting();

        for (int i = 0; i < sample; i++)
        {
            float4 RayColor = float4(1, 1, 1, 1);
            cache.BeginPath();

            float4 eye = mul(Camera.ModelViewInverse, float4(0, 0, 0, 1));
            float3 ray_dir = normalize(hitPrimaryVertex_.Position - eye.xyz);

            float3 direction = ray_dir;
            Vertex vertexStart = hitPrimaryVertex_;

            uint maxBounces = mat.MaterialModel == MaterialDielectric ? Camera.MaxNumberOfBounces : Camera.NumberOfBounces;

            // Accumulation flags / hit distance come from the first bounce only.
            bool hitReflect = false;
            bool hitMetal = false;
            float hitDist = 0;

            // Record the primary vertex as the path start.
            FDirectLighting startLighting = wantsDirect
                ? EvaluateDirectLighting(directIllum, vertexStart, direction)
                : FDirectLighting();
            float3 startDirect = startLighting.DiffuseDemodulated;
            bool pathContinue = cache.OnSurfaceHit(vertexStart.Position, vertexStart.Normal, startDirect, RandomSeed_);

            for (uint b = 0; b < maxBounces && pathContinue; ++b)
            {
                bool bHitReflect = false;
                bool bHitMetal = false;
                float bHitDist = 0;
                bool bHitMiss = false;
                float3 bTerminalRadiance = float3(0, 0, 0);
                float3 rayColorBefore = RayColor.rgb;
                const bool suppressDiffuseEmitter = (b == 0) || wantsDirect;
                const bool suppressSpecularEmitter = (b == 0);
                bool segmentExit = GetRayColor(tracer, vertexStart, direction, RayColor, RandomSeed_,
                                               suppressDiffuseEmitter, suppressSpecularEmitter,
                                               bHitReflect, bHitMetal, bHitDist, bHitMiss,
                                               bTerminalRadiance);

                if (b == 0)
                {
                    hitReflect = bHitReflect;
                    hitMetal = bHitMetal;
                    hitDist = bHitDist;
                }

                if (segmentExit)
                {
                    if (bHitMiss)
                    {
                        cache.OnMiss(bTerminalRadiance);
                    }
                    else if (any(bTerminalRadiance > float3(0, 0, 0)))
                    {
                        // Emissive terminator: record it as a hit with emissive radiance.
                        cache.OnSurfaceHit(vertexStart.Position, vertexStart.Normal, bTerminalRadiance, RandomSeed_);
                    }
                    break;
                }

                cache.OnSegmentThroughput(rayColorBefore, RayColor.rgb);
                FDirectLighting segmentLighting = wantsDirect
                    ? EvaluateDirectLighting(directIllum, vertexStart, direction)
                    : FDirectLighting();
                float3 segmentDirect = segmentLighting.DiffuseDemodulated;
                pathContinue = cache.OnSurfaceHit(vertexStart.Position, vertexStart.Normal, segmentDirect, RandomSeed_);
                if (!pathContinue)
                {
                    break;
                }

                float4 cachedRadiance = float4(0, 0, 0, 0);
                if (cache.TryQueryRadiance(vertexStart, bHitReflect, bHitMetal, b + 1u, bHitDist, cachedRadiance))
                {
                    RayColor *= cachedRadiance;
                    break;
                }

                if (b == 0)
                {
                    // First-bounce tail: optionally terminate immediately after the first hit.
                    if (ForceExitAfterFirst || (ExitAfterFirst && mat.MaterialModel != MaterialDielectric))
                    {
                        if (!FuzzyTracing)
                        {
                            float4 terminalRadiance = ApplyTerminalRadiance(vertexStart);
                            RayColor *= terminalRadiance;
                            if (UseAmbientCubeTerminal)
                            {
                                cache.OnMiss(terminalRadiance.rgb);
                            }
                        }
                        break;
                    }
                    // Otherwise fall through to the next bounce (no secondary direct, no RR at bounce 0).
                }
                else
                {
                    // Secondary (post-first-bounce) direct lighting strategy.
                    if (!bHitReflect)
                    {
                        if (SecondaryDirectMode == 1u)
                        {
                            FDirectLighting directLighting =
                                EvaluateDirectLighting(directIllum, vertexStart, direction);
                            float3 directRadiance = directLighting.DiffuseDemodulated;
                            if (any(directRadiance > float3(0, 0, 0)))
                            {
                                RayColor *= float4(directRadiance, 1);
                                break;
                            }
                        }
                        else if (SecondaryDirectMode == 0u && Camera.HasSun && (RandomFloat(RandomSeed_) < 0.5f))
                        {
                            const float3 lightVector = Camera.SunDirection.xyz;
                            const float3 lightVectorCone = AlignWithNormal(RandomInCone(RandomSeed_, cos(0.25f / 180.f * M_PI)), lightVector);
                            float shadowTraceOffset = TRACE_CORRECTION_OFFSET;
                            if (EnableSoftTracingAdaptiveOffset)
                            {
                                shadowTraceOffset = max(TRACE_CORRECTION_OFFSET, GetAmbientCubeUnitAtPosition(vertexStart.Position) * 0.5f);
                            }
                            if (!tracer.TraceOcclusion(vertexStart.Position + vertexStart.Normal * shadowTraceOffset, lightVectorCone))
                            {
                                RayColor *= Camera.SunColor;
                                break;
                            }
                        }
                    }

                    // 提前退出
                    const bool earlyExit = (mat.MaterialModel != MaterialDielectric) && (RandomFloat(RandomSeed_) < ExitProbability);

                    // 终结路径
                    if (b == maxBounces - 1 || earlyExit)
                    {
                        if (mat.MaterialModel == MaterialDielectric)
                        {
                            RayColor *= float4(0, 0, 0, 1);
                        }
                        else
                        {
                            float4 terminalRadiance = ApplyTerminalRadiance(vertexStart);
                            RayColor *= terminalRadiance;
                            if (UseAmbientCubeTerminal)
                            {
                                cache.OnMiss(terminalRadiance.rgb);
                            }
                        }
                        break;
                    }
                }
            }

            if (hitMetal)
            {
                RayColor *= primaryAlbedo_;
            }

            if (hitReflect)
            {
                FinalReflection += RayColor;
                FinalSpecularHitDist += hitDist;
            }
            else
            {
                FinalColor += RayColor;
                FinalDiffuseHitDist += hitDist;
            }

        }

        FinalColor /= float(sample);
        FinalReflection /= float(sample);
        FinalDiffuseHitDist /= float(sample);
        FinalSpecularHitDist /= float(sample);

        Material primaryMaterial = Bindless.GetGpuscene().GetMaterial(hitPrimaryVertex_.MaterialIndex);
        FBSDFContext primaryContext =
            BuildBSDFContext(hitPrimaryVertex_, primaryMaterial, primaryRayDir_);
        FDirectLighting directLighting = FDirectLighting();
        directIllum.DirectIlluminatePrimary(RandomSeed_, pixel_, primaryContext, directLighting);
        FinalColor.rgb += max(directLighting.DiffuseDemodulated, float3(0, 0, 0));
        FinalReflection.rgb += max(directLighting.SpecularRadiance, float3(0, 0, 0));
        FinalReflection.rgb += max(EvaluateGlossyDirect(tracer, primaryContext,
                                                        hitPrimaryVertex_, HitNormalOffset,
                                                        RandomSeed_), float3(0, 0, 0));

        if (WriteOutputs)
        {
            OutSingleDiffuse.Store(pixel_, FinalColor);
            OutSingleSpecular.Store(pixel_, FinalReflection);
            OutDiffuseHitDist.Store(pixel_, FinalDiffuseHitDist + primaryRayLength_);
            OutSpecularHitDist.Store(pixel_, FinalSpecularHitDist + primaryRayLength_);
        }
    }

    [mutating]
    public bool PrimaryHit(IPrimaryRayCaster primaryRayCaster)
    {
        let Camera = Bindless.GetGpuscene().Camera[0];

        if (!primaryRayCaster.TracePrimaryRay(pixel_, size_, RandomSeed_, hitPrimaryVertex_, hitPrimaryNode_, primaryRayDir_, primaryRayLength_, primaryPixelOffset_))
        {
            float4 skyColor = Camera.HasSky ? Common.SampleIBL(Camera.SkyIdx, primaryRayDir_, Camera.SkyRotation, 0) * Camera.SkyIntensity : float4(0, 0, 0, 0);
            skyColor.rgb += Common.EvaluateAnalyticSunDisk(primaryRayDir_);

            if (WriteOutputs)
            {
                OutSingleDiffuse.Store(pixel_, skyColor);
                OutMotionVector.Store(pixel_, float4(0, 0, 0, 0));
                OutAlbedo.Store(pixel_, float4(1, 1, 1, 1));
                OutNormal.Store(pixel_, float4(0, 1, 0, 1));
                OutObjectID.Store(pixel_, 0);
                OutDiffuseHitDist.Store(pixel_, 1000);
                OutSpecularHitDist.Store(pixel_, 1000);
                OutSpecularAlbedo.Store(pixel_, float4(0, 0, 0, 0));
                OutBSDFData.Store(pixel_, uint2(0xFFFFFFFFu, 0u));
            }

            return false;
        }

        float4 clipPos = mul(Camera.ViewProjection, float4(hitPrimaryVertex_.Position, 1.0));
        float ndcDepth = clipPos.z / clipPos.w;
        float2 motion = Common.CalculateMotionVector(Camera, hitPrimaryNode_, hitPrimaryVertex_, pixel_);
        primaryMotionPixels_ = motion * size_;
        float4 gbuffer = float4(0, 0, 0, 0);
        Material material = Bindless.GetGpuscene().GetMaterial(hitPrimaryVertex_.MaterialIndex);
        Common.FetchGBuffer(hitPrimaryVertex_, material, hitPrimaryNode_, primaryRayDir_, primaryAlbedo_, gbuffer);
        FBSDFContext outputContext =
            BuildBSDFContext(hitPrimaryVertex_, material, primaryRayDir_);

        if (WriteOutputs)
        {
            OutAlbedo.Store(pixel_, primaryAlbedo_);
            OutNormal.Store(pixel_, gbuffer);
            const bool selected = hitPrimaryNode_.reserved1 != 0;
            const bool hovered = (hitPrimaryNode_.reserved2 & 1u) != 0;
            const bool locked = (hitPrimaryNode_.reserved2 & 2u) != 0;
            const bool danger = (hitPrimaryNode_.reserved2 & 4u) != 0;
            OutObjectID.Store(pixel_, Common.EncodeObjectId(hitPrimaryNode_.instanceId, selected, hovered, locked, danger));
            // The engine-wide upscaler contract stores motion in render-pixel
            // units. Streamline and FidelityFX can therefore both use a 1:1
            // motion-vector scale.
            OutMotionVector.Store(pixel_, float4(primaryMotionPixels_, 0, 0));
            OutPrevDepth.Store(pixel_, ndcDepth);
            OutBSDFData.Store(pixel_, uint2(hitPrimaryVertex_.MaterialIndex,
                                           asuint(outputContext.Metalness)));
        }

        if (material.MaterialModel == MaterialDiffuseLight)
        {
            if (WriteOutputs)
            {
                OutAlbedo.Store(pixel_, float4(1, 1, 1, 0));
                OutSingleDiffuse.Store(pixel_, primaryAlbedo_);
                OutSingleSpecular.Store(pixel_, float4(0, 0, 0, 0));
                OutDiffuseHitDist.Store(pixel_, 10);
                OutSpecularHitDist.Store(pixel_, 10);
                OutSpecularAlbedo.Store(pixel_, float4(0, 0, 0, 0));
                OutBSDFData.Store(pixel_, uint2(0xFFFFFFFFu, 0u));
            }
            return false;
        }

        float4 specAlbedo = material.MaterialModel == MaterialMetallic ? primaryAlbedo_ : float4(1, 1, 1, 1);
        if (WriteOutputs)
        {
            OutSpecularAlbedo.Store(pixel_, specAlbedo);
        }

        if (material.MaterialModel != MaterialDielectric)
        {
            hitPrimaryVertex_.Position = hitPrimaryVertex_.Position - primaryRayDir_ * Camera.SceneEpsilonScale * 0.01f;// - rayDir * rayLength * 0.005;
        }

        return true;
    }

    public uint2 GetSize() { return size_; }
    public Vertex GetPrimaryVertex() { return hitPrimaryVertex_; }
    // Primary-hit data for consumers inside the same dispatch (ReSTIR temporal gates).
    // Reading the freshly written G-buffer back is NOT coherent within an invocation, so
    // this state must come from the renderer, never from the output textures.
    public uint GetPrimaryInstanceId() { return hitPrimaryNode_.instanceId; }
    public float2 GetPrimaryMotionPixels() { return primaryMotionPixels_; }

    // Debug write that only replaces the lighting color and leaves the real G-buffer
    // (object id, motion, normals) intact. Debug overlays that run every frame must use
    // this, or they destroy the very temporal state they are trying to visualize.
    public void WriteDebugColorOnly(float4 color)
    {
        OutSingleDiffuse.Store(pixel_, color);
        OutSingleSpecular.Store(pixel_, float4(0, 0, 0, 0));
    }

    // Generic debug-color G-buffer write (used by entry-shader debug views).
    public void WriteDebugColor(float4 color)
    {
        OutSingleDiffuse.Store(pixel_, color);
        OutSingleSpecular.Store(pixel_, float4(0, 0, 0, 0));
        OutAlbedo.Store(pixel_, float4(1, 1, 1, 1));
        OutNormal.Store(pixel_, float4(0, 1, 0, 1));
        OutObjectID.Store(pixel_, 0);
        OutMotionVector.Store(pixel_, float4(0, 0, 0, 0));
        OutPrevDepth.Store(pixel_, 1.0f);
        OutDiffuseHitDist.Store(pixel_, 1000.0f);
        OutSpecularHitDist.Store(pixel_, 1000.0f);
        OutSpecularAlbedo.Store(pixel_, float4(0, 0, 0, 0));
        OutBSDFData.Store(pixel_, uint2(0xFFFFFFFFu, 0u));
    }

    // Debug view: cached radiance modulated by primary albedo. The cache query
    // is performed by the entry shader and its result passed in here so the
    // renderer stays free of any cache-specific symbols.
    public void WriteDebugCachedRadiance(float4 cachedRadiance, bool hit)
    {
        float4 outColor = float4(0, 0, 0, 1);
        if (hit)
        {
            outColor = float4(max(cachedRadiance.rgb * primaryAlbedo_.rgb, float3(0, 0, 0)), 1);
        }
        WriteDebugColor(outColor);
    }

    [mutating]
    public void Init(uint2 ipos)
    {
        let Camera = Bindless.GetGpuscene().Camera[0];
        pixel_ = ipos;
        RandomSeed_ = InitRandomSeed(ipos.x, ipos.y, Camera.TotalFrames);
    }

    public __init()
    {
        pixel_ = int2(0, 0);
        size_ = Bindless.GetViewStorageTextureDimensions<float4>(Bindless.RT_SINGLE_DIFFUSE);

        RandomSeed_ = uint4(0, 0, 0, 0);
        hitPrimaryVertex_ = {};
        hitPrimaryNode_ = {};
        primaryRayDir_ = float3(0, 1, 0);
        primaryRayLength_ = 0;
        primaryPixelOffset_ = float2(0, 0);
        primaryMotionPixels_ = float2(0, 0);
        primaryAlbedo_ = float4(0, 0, 0, 0);

        OutAlbedo = Bindless.GetViewStorageTexture<float4>(Bindless.RT_ALBEDO);
        OutNormal = Bindless.GetViewStorageTexture<float4>(Bindless.RT_NORMAL);
        OutObjectID = Bindless.GetViewStorageTexture<uint>(Bindless.RT_OBJECTID_0);
        OutMotionVector = Bindless.GetViewStorageTexture<float4>(Bindless.RT_MOTIONVECTOR);
        OutPrevDepth = Bindless.GetViewStorageTexture<float>(Bindless.RT_PREV_DEPTHBUFFER);
        OutSingleDiffuse = Bindless.GetViewStorageTexture<float4>(Bindless.RT_SINGLE_DIFFUSE);
        OutSingleSpecular = Bindless.GetViewStorageTexture<float4>(Bindless.RT_SINGLE_SPECULAR);
        OutDiffuseHitDist = Bindless.GetViewStorageTexture<float>(Bindless.RT_DIFFUSE_HITDIST);
        OutSpecularHitDist = Bindless.GetViewStorageTexture<float>(Bindless.RT_SPECULAR_HITDIST);
        OutSpecularAlbedo = Bindless.GetViewStorageTexture<float4>(Bindless.RT_SPECULAR_ALBEDO);
        OutBSDFData = Bindless.GetViewStorageTexture<uint2>(Bindless.RT_BSDF_DATA);
    }

    int2 pixel_;
    uint2 size_;
    uint4 RandomSeed_;

    Vertex hitPrimaryVertex_;
    NodeProxy hitPrimaryNode_;
    float3 primaryRayDir_;
    float primaryRayLength_;
    float2 primaryPixelOffset_;
    float2 primaryMotionPixels_;
    float4 primaryAlbedo_;

    RWTexture2D<float4> OutAlbedo;
    RWTexture2D<float4> OutNormal;
    RWTexture2D<uint> OutObjectID;
    RWTexture2D<float4> OutMotionVector;
    RWTexture2D<float> OutPrevDepth;
    RWTexture2D<float4> OutSingleDiffuse;
    RWTexture2D<float4> OutSingleSpecular;
    RWTexture2D<float> OutDiffuseHitDist;
    RWTexture2D<float> OutSpecularHitDist;
    RWTexture2D<float4> OutSpecularAlbedo;
    RWTexture2D<uint2> OutBSDFData;
}
