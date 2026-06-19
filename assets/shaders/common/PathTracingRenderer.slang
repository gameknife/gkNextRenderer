implementing Common;
#include "PreProcessor.slang"

// Direct illumination strategies + path-tracing renderer split out of Shading.slang.
// Houses:
//   - FHardwareDirectIlluminator   : sun shadow via RayQuery
//   - FAmbientCubeIlluminator      : null illuminator (probe-only path)
//   - FSoftwareDirectIlluminator   : cascade-aware DDA shadow
//   - FShadowMapDirectIlluminator  : shadow-map sampling
//   - FPathTracingRenderer         : multi-bounce path tracer driving any IRayTracer + IDirectIlluminator
// IRayTracer impls live in RayTracers.slang; primary ray casters and interfaces stay in Shading.slang.

public struct FHardwareDirectIlluminator : IDirectIlluminator
{
    public void DirectIlluminate(inout uint4 RandomSeed, float3 position, float3 normal, inout float4 directIllumColor)
    {
        UniformBufferObject Camera = Bindless.GetGpuscene().Camera[0];
        float shadow = 0.0f;
        const float3 lightVector = Camera.SunDirection.xyz;
        const float4 d = max(dot(lightVector, normalize(normal)), 0.0) * M_1_PI;
        if (Camera.HasSun)
        {
            const float3 lightVector = Camera.SunDirection.xyz;
            const float3 lightVectorCone = AlignWithNormal(RandomInCone(RandomSeed, cos(0.25f / 180.f * M_PI)), lightVector);
            shadow = 1;
            if (HWTracer.TraceOcclusion(position, lightVectorCone))
            {
                shadow = 0;
            }
        }
        directIllumColor = Camera.SunColor * d * shadow;
    }

    FHardwareRayTracer HWTracer;
}

public struct FAmbientCubeIlluminator : IDirectIlluminator
{
    public void DirectIlluminate(inout uint4 RandomSeed, float3 position, float3 normal, inout float4 directIllumColor)
    {
        directIllumColor = float4(0, 0, 0, 0);
    }
}

public struct FSoftwareDirectIlluminator : IDirectIlluminator
{
    public void DirectIlluminate(inout uint4 RandomSeed, float3 position, float3 normal, inout float4 directIllumColor)
    {
        UniformBufferObject Camera = Bindless.GetGpuscene().Camera[0];
        float shadow = 0.0f;
        const float3 lightVector = Camera.SunDirection.xyz;
        const float4 d = max(dot(lightVector, normalize(normal)), 0.0) * M_1_PI;
        if (Camera.HasSun)
        {
            const uint cascadeIndex = GetAmbientCubeCascadeAtPosition(position);
            const float cascadeUnit = GetAmbientCubeUnitAtCascade(cascadeIndex);
            const float3 lightVector = Camera.SunDirection.xyz;
            const float3 lightVectorCone = AlignWithNormal(RandomInCone(RandomSeed, cos(0.25f / 180.f * M_PI)), lightVector);
            shadow = 1;
            float3 normalDir = normalize(normal);
            float normalOffset = max(cascadeUnit * 0.5f, 0.25f);
            float rayOffset = cascadeUnit * 2.5f;
            if (cascadeIndex > 0)
            {
                normalOffset = cascadeUnit * (1.0f + 0.5f * float(cascadeIndex));
                rayOffset = cascadeUnit * (3.0f + float(cascadeIndex));
            }
            float3 shadowOrigin = position + normalDir * normalOffset + lightVectorCone * rayOffset;
            if (HWTracer.TraceOcclusion(shadowOrigin, lightVectorCone))
            {
                shadow = 0;
            }
        }
        directIllumColor = Camera.SunColor * d * shadow;
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
    public void DirectIlluminate(inout uint4 RandomSeed, float3 position, float3 normal, inout float4 directIllumColor)
    {
        UniformBufferObject Camera = Bindless.GetGpuscene().Camera[0];
        const float3 lightVector = Camera.SunDirection.xyz;
        const float4 d = max(dot(lightVector, normalize(normal)), 0.0) * M_1_PI;

        float shadow = 0.0f;
        if (Camera.HasSun)
        {
            float viewDist = -mul(Camera.ModelView, float4(position, 1.0f)).z;
            shadow = Common.SampleSunShadowCSM(position, normal, viewDist);
        }
        directIllumColor = Camera.SunColor * d * shadow;
    }
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
    public float3 EvaluateDirectLighting(IDirectIlluminator directIllum, Vertex vertex)
    {
        float4 directIllumColor = float4(0, 0, 0, 0);
        directIllum.DirectIlluminate(RandomSeed_, vertex.Position, vertex.Normal, directIllumColor);
        return max(directIllumColor.rgb, float3(0, 0, 0));
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

    bool GetRayColor(IRayTracer tracer, inout Vertex inVertex, inout float3 rayDir, inout float4 rayColor, inout uint4 RandomSeed, inout bool hitReflect, inout bool hitMetal, out float outHitDist, out bool outHitMiss, out float3 outTerminalRadiance)
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
        if (mat.MaterialModel == MaterialDiffuseLight)
        {
            FinalColor = min(mat.Diffuse, float4(1000, 1000, 1000, 1));
            if (WriteOutputs)
            {
                OutSingleDiffuse.Store(pixel_, FinalColor);
                OutSingleSpecular.Store(pixel_, FinalReflection);
                OutDiffuseHitDist.Store(pixel_, primaryRayLength_);
                OutSpecularHitDist.Store(pixel_, primaryRayLength_);
            }
            return;
        }

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
            float3 startDirect = wantsDirect ? EvaluateDirectLighting(directIllum, vertexStart) : float3(0, 0, 0);
            bool pathContinue = cache.OnSurfaceHit(vertexStart.Position, vertexStart.Normal, startDirect, RandomSeed_);

            for (uint b = 0; b < maxBounces && pathContinue; ++b)
            {
                bool bHitReflect = false;
                bool bHitMetal = false;
                float bHitDist = 0;
                bool bHitMiss = false;
                float3 bTerminalRadiance = float3(0, 0, 0);
                float3 rayColorBefore = RayColor.rgb;
                bool segmentExit = GetRayColor(tracer, vertexStart, direction, RayColor, RandomSeed_, bHitReflect, bHitMetal, bHitDist, bHitMiss, bTerminalRadiance);

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
                float3 segmentDirect = wantsDirect ? EvaluateDirectLighting(directIllum, vertexStart) : float3(0, 0, 0);
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
                            float3 directRadiance = EvaluateDirectLighting(directIllum, vertexStart);
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

        float4 directColor = float4(0, 0, 0, 0);
        directIllum.DirectIlluminate(RandomSeed_, hitPrimaryVertex_.Position, hitPrimaryVertex_.Normal, directColor);
        FinalColor += directColor;

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
            }

            return false;
        }

        float4 clipPos = mul(Camera.ViewProjection, float4(hitPrimaryVertex_.Position, 1.0));
        float ndcDepth = clipPos.z / clipPos.w;
        float2 motion = Common.CalculateMotionVector(Camera, hitPrimaryNode_, hitPrimaryVertex_, pixel_);
        float4 gbuffer = float4(0, 0, 0, 0);
        Material material = Bindless.GetGpuscene().GetMaterial(hitPrimaryVertex_.MaterialIndex);
        Common.FetchGBuffer(hitPrimaryVertex_, material, hitPrimaryNode_, primaryRayDir_, primaryAlbedo_, gbuffer);

        if (WriteOutputs)
        {
            OutAlbedo.Store(pixel_, primaryAlbedo_);
            OutNormal.Store(pixel_, gbuffer);
            const bool selected = hitPrimaryNode_.reserved1 != 0;
            const bool hovered = (hitPrimaryNode_.reserved2 & 1u) != 0;
            const bool locked = (hitPrimaryNode_.reserved2 & 2u) != 0;
            const bool danger = (hitPrimaryNode_.reserved2 & 4u) != 0;
            OutObjectID.Store(pixel_, Common.EncodeObjectId(hitPrimaryNode_.instanceId, selected, hovered, locked, danger));
            OutMotionVector.Store(pixel_, float4(motion * size_, 0, 0));
            OutPrevDepth.Store(pixel_, ndcDepth);
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
        size_ = Bindless.GetStorageTextureDimensions<float4>(Bindless.RT_SINGLE_DIFFUSE);

        RandomSeed_ = uint4(0, 0, 0, 0);
        hitPrimaryVertex_ = {};
        hitPrimaryNode_ = {};
        primaryRayDir_ = float3(0, 1, 0);
        primaryRayLength_ = 0;
        primaryPixelOffset_ = float2(0, 0);
        primaryAlbedo_ = float4(0, 0, 0, 0);

        OutAlbedo = Bindless.GetStorageTexture<float4>(Bindless.RT_ALBEDO);
        OutNormal = Bindless.GetStorageTexture<float4>(Bindless.RT_NORMAL);
        OutObjectID = Bindless.GetStorageTexture<uint>(Bindless.RT_OBJEDCTID_0);
        OutMotionVector = Bindless.GetStorageTexture<float4>(Bindless.RT_MOTIONVECTOR);
        OutPrevDepth = Bindless.GetStorageTexture<float>(Bindless.RT_PREV_DEPTHBUFFER);
        OutSingleDiffuse = Bindless.GetStorageTexture<float4>(Bindless.RT_SINGLE_DIFFUSE);
        OutSingleSpecular = Bindless.GetStorageTexture<float4>(Bindless.RT_SINGLE_SPECULAR);
        OutDiffuseHitDist = Bindless.GetStorageTexture<float>(Bindless.RT_DIFFUSE_HITDIST);
        OutSpecularHitDist = Bindless.GetStorageTexture<float>(Bindless.RT_SPECULAR_HITDIST);
        OutSpecularAlbedo = Bindless.GetStorageTexture<float4>(Bindless.RT_SPECULAR_ALBEDO);
    }

    int2 pixel_;
    uint2 size_;
    uint4 RandomSeed_;

    Vertex hitPrimaryVertex_;
    NodeProxy hitPrimaryNode_;
    float3 primaryRayDir_;
    float primaryRayLength_;
    float2 primaryPixelOffset_;
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
}
