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

public struct FShadowMapDirectIlluminator : IDirectIlluminator
{
    public float getShadow(float3 worldPos, float3 jit, float3 normal )
    {
        float4 posInLightMap = mul(Camera.SunCascadeViewProjection[0], float4(worldPos + jit * 4.0f, 1.0f));

        float3 projCoords = posInLightMap.xyz / posInLightMap.w;
        projCoords = projCoords * 0.5 + 0.5;
        projCoords.y = 1.0 - projCoords.y;

        float currentDepth = projCoords.z;
        float cosTheta = max(dot(normal, normalize(Camera.SunDirection.xyz)), 0.0);
        float bias = lerp(0.0001, 0.00005, cosTheta);

        float closestDepth = ShadowMapSampler.SampleLevel(projCoords.xy, 0).x;
        float shadow = currentDepth - bias > closestDepth ? 0.0 : 1.0;

        // if (shadow > 0.01) {
        //     float3 outPosition;
        //     float3 outNormal;
        //     uint outMaterialIdx;
        //     float contactShadow = Common.traceInScreenSpace(worldPos + jit, normalize(Camera.SunDirection.xyz), 0.25, 0.5, outPosition, outNormal, outMaterialIdx, Camera, MiniGBuffer, NodeProxies, Models, Vertices, Indices) ? 0.0 : 1.0;
        //     shadow = min(shadow, contactShadow); // 结合两种阴影结果
        // }

        return shadow;
    };


    public void DirectIlluminate(inout uint4 RandomSeed, float3 position, float3 normal, inout float4 directIllumColor)
    {
        float shadow = 0.0f;
        const float3 lightVector = Camera.SunDirection.xyz;
        const float4 d = max(dot(lightVector, normalize(normal)), 0.0) * M_1_PI;
        if (Camera.HasSun)
        {
            float3 jitter = (RandomFloat3(RandomSeed) - float3(0.5f)) * 0.125f;
            shadow += getShadow(position, jitter, normal);
        }
        directIllumColor = Camera.SunColor * d * shadow;
    }

    ConstantBuffer<UniformBufferObject> Camera;
    Sampler2D ShadowMapSampler;

    RWTexture2D<uint> MiniGBuffer;
    StructuredBuffer<NodeProxy> NodeProxies;
    StructuredBuffer<ModelData> Models;
    StructuredBuffer<GPUVertex> Vertices;
    StructuredBuffer<uint> Indices;
}

public struct FPathTracingRenderer{
    public float ExitProbability = 0.5;
    public bool ExitAfterFirst = false;
    public float HitNormalOffset = 0.0f;
    public int SampleDownscale = 1;
    public bool FuzzyTracing = false;
    public bool ForceExitAfterFirst = false;
    public bool EnableSoftTracingAdaptiveOffset = false;

    bool GetRayColor(IRayTracer tracer, inout Vertex inVertex, inout float3 rayDir, inout float4 rayColor, inout uint4 RandomSeed, inout bool hitReflect, inout bool hitMetal, out float outHitDist)
    {
        UniformBufferObject Camera = Bindless.GetGpuscene().Camera[0];

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
            }
            outHitDist = PT_MAX_TRACE_DISTANCE;
            return true;
        }
    }

    [mutating]
    public void Render(IRayTracer tracer, IDirectIlluminator directIllum, int sampleMultiplier)
    {
        UniformBufferObject Camera = Bindless.GetGpuscene().Camera[0];

        float4 FinalColor = float4(0, 0, 0, 0);
        float4 FinalReflection = float4(0, 0, 0, 0);
        float FinalDiffuseHitDist = 0;
        float FinalSpecularHitDist = 0;

        Material mat = Bindless.GetGpuscene().GetMaterial(hitPrimaryVertex_.MaterialIndex);
        if (mat.MaterialModel == MaterialDiffuseLight)
        {
            FinalColor = min(mat.Diffuse, float4(1000, 1000, 1000, 1));
            OutSingleDiffuse.Store(pixel_, FinalColor);
            OutSingleSpecular.Store(pixel_, FinalReflection);
            OutDiffuseHitDist.Store(pixel_, primaryRayLength_);
            OutSpecularHitDist.Store(pixel_, primaryRayLength_);
            return;
        }

        const int sample = max(1, Camera.NumberOfSamples / SampleDownscale * sampleMultiplier);

        for (int i = 0; i < sample; i++)
        {
            float4 RayColor = float4(1, 1, 1, 1);

            float4 eye = mul(Camera.ModelViewInverse, float4(0, 0, 0, 1));
            float3 ray_dir = normalize(hitPrimaryVertex_.Position - eye.xyz);

            float3 direction = ray_dir;
            Vertex vertexStart = hitPrimaryVertex_;

            uint maxBounces = mat.MaterialModel == MaterialDielectric ? Camera.MaxNumberOfBounces : Camera.NumberOfBounces;

            bool hitReflect = false;
            bool hitMetal = false;
            float hitDist = 0;
            bool exitFirst = GetRayColor(tracer, vertexStart, direction, RayColor, RandomSeed_, hitReflect, hitMetal, hitDist);
            if (!exitFirst)
            {
                if(ForceExitAfterFirst || (ExitAfterFirst && mat.MaterialModel != MaterialDielectric) )
                {
                    if(!FuzzyTracing) RayColor *= interpolateAmbientCubes<FullAmbientCubeSampler>(vertexStart.Position, vertexStart.Normal);
                }
                else
                {
                    for (uint b = 1; b < maxBounces; ++b)
                    {
                        bool hitReflectDontCare = false;
                        bool hitMetalDontCare = false;
                        float hitDistDontCare = 0;
                        if (GetRayColor(tracer, vertexStart, direction, RayColor, RandomSeed_, hitReflectDontCare, hitMetalDontCare, hitDistDontCare))
                        {
                            break;
                        }

                        if (!hitReflectDontCare && Camera.HasSun && (RandomFloat(RandomSeed_) < 0.5f))
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

                        // 提前退出
                        const bool earlyExit = b > 0 && (mat.MaterialModel != MaterialDielectric) && (RandomFloat(RandomSeed_) < ExitProbability);

                        // 终结路径
                        if (b == maxBounces - 1 || earlyExit)
                        {
                            if (mat.MaterialModel == MaterialDielectric)
                            {
                                RayColor *= float4(0, 0, 0, 1);
                            }
                            else
                            {
                                RayColor *= interpolateAmbientCubes<FullAmbientCubeSampler>(vertexStart.Position, vertexStart.Normal);
                            }
                            break;
                        }
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

        OutSingleDiffuse.Store(pixel_, FinalColor);
        OutSingleSpecular.Store(pixel_, FinalReflection);
        OutDiffuseHitDist.Store(pixel_, FinalDiffuseHitDist + primaryRayLength_);
        OutSpecularHitDist.Store(pixel_, FinalSpecularHitDist + primaryRayLength_);
    }

    [mutating]
    public bool PrimaryHit(IPrimaryRayCaster primaryRayCaster)
    {
        let Camera = Bindless.GetGpuscene().Camera[0];

        if (!primaryRayCaster.TracePrimaryRay(pixel_, size_, RandomSeed_, hitPrimaryVertex_, hitPrimaryNode_, primaryRayDir_, primaryRayLength_, primaryPixelOffset_))
        {
            float4 skyColor = Camera.HasSky ? Common.SampleIBL(Camera.SkyIdx, primaryRayDir_, Camera.SkyRotation, 0) * Camera.SkyIntensity : float4(0, 0, 0, 0);

            OutSingleDiffuse.Store(pixel_, skyColor);
            OutMotionVector.Store(pixel_, float4(0, 0, 0, 0));
            OutAlbedo.Store(pixel_, float4(1, 1, 1, 1));
            OutNormal.Store(pixel_, float4(0, 1, 0, 1));
            OutObjectID.Store(pixel_, 0);
            OutDiffuseHitDist.Store(pixel_, 1000);
            OutSpecularHitDist.Store(pixel_, 1000);
            OutSpecularAlbedo.Store(pixel_, float4(0, 0, 0, 0));

            return false;
        }

        float4 clipPos = mul(Camera.ViewProjection, float4(hitPrimaryVertex_.Position, 1.0));
        float ndcDepth = clipPos.z / clipPos.w;
        float2 motion = Common.CalculateMotionVector(Camera, hitPrimaryNode_, hitPrimaryVertex_, pixel_);
        float4 gbuffer = float4(0, 0, 0, 0);
        Material material = Bindless.GetGpuscene().GetMaterial(hitPrimaryVertex_.MaterialIndex);
        Common.FetchGBuffer(hitPrimaryVertex_, material, hitPrimaryNode_, primaryRayDir_, primaryAlbedo_, gbuffer);

        OutAlbedo.Store(pixel_, primaryAlbedo_);
        OutNormal.Store(pixel_, gbuffer);
        const bool selected = hitPrimaryNode_.reserved1 != 0;
        const bool hovered = (hitPrimaryNode_.reserved2 & 1u) != 0;
        const bool locked = (hitPrimaryNode_.reserved2 & 2u) != 0;
        const bool danger = (hitPrimaryNode_.reserved2 & 4u) != 0;
        OutObjectID.Store(pixel_, Common.EncodeObjectId(hitPrimaryNode_.instanceId, selected, hovered, locked, danger));
        OutMotionVector.Store(pixel_, float4(motion * size_, 0, 0));
        OutPrevDepth.Store(pixel_, ndcDepth);

        float4 specAlbedo = material.MaterialModel == MaterialMetallic ? primaryAlbedo_ : float4(1, 1, 1, 1);
        OutSpecularAlbedo.Store(pixel_, specAlbedo);

        if (material.MaterialModel != MaterialDielectric)
        {
            hitPrimaryVertex_.Position = hitPrimaryVertex_.Position - primaryRayDir_ * Camera.SceneEpsilonScale * 0.01f;// - rayDir * rayLength * 0.005;
        }

        return true;
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
