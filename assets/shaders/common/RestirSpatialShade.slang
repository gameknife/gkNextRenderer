// Shared ReSTIR DI pass 2. Hardware and software entry shaders provide an IRayTracer;
// reservoir combination and shading stay single-source.

static const float kRestirGoldenAngle = 2.39996323f;

// Must mirror FVisibilityBufferRayCaster: no half-pixel offset.
public float3 RestirReconstructViewPosition(
    UniformBufferObject camera, int2 pixel, uint2 size, float depth)
{
    const float2 ndc = (float2(pixel) / float2(size)) * 2.0f - 1.0f;
    const float4 view = mul(camera.ProjectionInverse, float4(ndc, depth, 1.0f));
    const float safeW = abs(view.w) > 1e-6f ? view.w : (view.w >= 0.0f ? 1e-6f : -1e-6f);
    return view.xyz / safeW;
}

public void RestirSpatialShade(uint3 dispatchThreadId, IRayTracer tracer)
{
    const uint2 size =
        Bindless.GetViewStorageTextureDimensions<float4>(Bindless.RT_SINGLE_DIFFUSE);
    if (any(dispatchThreadId.xy >= size))
    {
        return;
    }
    if (!RestirIsAvailable() || Bindless.GetGpuscene().CustomData0 != 0)
    {
        return;
    }

    FRestirRuntimeParameters params = RestirGetRuntimeParameters();
    const int2 pixel = int2(dispatchThreadId.xy);
    if (uint(pixel.x) >= params.ReservoirWidth || uint(pixel.y) >= params.ReservoirHeight)
    {
        return;
    }
    const uint reservoirIndex = RestirReservoirIndex(pixel, params);

    GPUScene gpuScene = Bindless.GetGpuscene();
    UniformBufferObject camera = gpuScene.Camera[0];
    let objectId0 = Bindless.GetViewStorageTexture<uint>(Bindless.RT_OBJECTID_0);
    let normalBuffer = Bindless.GetViewStorageTexture<float4>(Bindless.RT_NORMAL);
    let albedoBuffer = Bindless.GetViewStorageTexture<float4>(Bindless.RT_ALBEDO);
    let bsdfDataBuffer = Bindless.GetViewStorageTexture<uint2>(Bindless.RT_BSDF_DATA);
    let depthBuffer = Bindless.GetViewStorageTexture<float>(Bindless.RT_PREV_DEPTHBUFFER);
    let singleDiffuse = Bindless.GetViewStorageTexture<float4>(Bindless.RT_SINGLE_DIFFUSE);

    const uint centerPrim = RestirFetchPrimitiveIndex(objectId0[pixel]);
    FRestirState center =
        RestirUnpackReservoir(RestirGetIntermediateBuffer(params)[reservoirIndex]);

    // Every pixel writes final history, including sky/emitter/empty pixels.
    if (centerPrim == RESTIR_OBJID_INVALID || center.SampleType == RESTIR_SAMPLE_NONE)
    {
        FRestirState empty = FRestirState();
        empty.M = center.M;
        RestirGetFinalBuffer(params)[reservoirIndex] = RestirPackReservoir(empty);
        if (params.DebugMode != 0)
        {
            singleDiffuse[pixel] = RestirDebugColor(empty, params);
        }
        return;
    }

    const float4 normalData = normalBuffer[pixel];
    const float3 normal = normalData.rgb;
    const float centerDepth = depthBuffer[pixel];
    const float3 centerView =
        RestirReconstructViewPosition(camera, pixel, size, centerDepth);
    float3 position = mul(camera.ModelViewInverse, float4(centerView, 1.0f)).xyz;
    const float3 eye = mul(camera.ModelViewInverse, float4(0, 0, 0, 1)).xyz;
    position -= normalize(position - eye) * camera.SceneEpsilonScale * 0.01f;

    const uint2 packedBsdf = bsdfDataBuffer[pixel];
    if (packedBsdf.x == 0xFFFFFFFFu)
    {
        RestirGetFinalBuffer(params)[reservoirIndex] = RestirPackReservoir(FRestirState());
        return;
    }
    Material material = gpuScene.GetMaterial(int(packedBsdf.x));
    if (material.MaterialModel != MaterialLambertian &&
        material.MaterialModel != MaterialMixture)
    {
        RestirGetFinalBuffer(params)[reservoirIndex] = RestirPackReservoir(FRestirState());
        return;
    }
    FBSDFContext context;
    context.Position = position;
    context.Normal = normalize(normal);
    context.Wo = normalize(eye - position);
    context.BaseColor = albedoBuffer[pixel].rgb;
    context.PerceptualRoughness = saturate(normalData.a);
    context.Metalness = saturate(asfloat(packedBsdf.y));
    context.EtaOutside = 1.0f;
    context.EtaInside = max(material.RefractionIndex2, 1.0001f);
    context.MaterialModel = material.MaterialModel;
    context.FrontFace = true;

    uint4 randomSeed = InitRandomSeed(
        dispatchThreadId.x, dispatchThreadId.y, camera.TotalFrames + 7777u);
    const float reconstructionOffset = max(EPS2, abs(centerView.z) * 1e-4f);

    // Verify center first; only this post-visibility center reservoir enters temporal history.
    if (center.SampleType == RESTIR_SAMPLE_AREA_LIGHT && center.WeightW > 0.0f)
    {
        float3 centerF;
        float3 centerLightPosition;
        if (!Common.EvaluateAreaLightDiffuseSample(
                center.LightIndex, center.LightUV, context,
                centerF, centerLightPosition))
        {
            center.WeightW = 0.0f;
        }
        else
        {
            const float3 shadowOrigin = tracer.GetAreaLightShadowOrigin(
                context.Position, context.Normal, centerLightPosition, reconstructionOffset);
            if (tracer.TraceAreaLightSegment(shadowOrigin, centerLightPosition, EPS))
            {
                center.WeightW = 0.0f;
            }
        }
    }

    RestirGetFinalBuffer(params)[reservoirIndex] = RestirPackReservoir(center);

    FRestirState combined = FRestirState();
    combined.Merge(center, center.TargetLum, RandomFloat(randomSeed));

    if ((params.Flags & RESTIR_FLAG_SPATIAL_ENABLE) != 0 && params.SpatialSamples > 0)
    {
        const float angleOffset = RandomFloat(randomSeed) * 2.0f * M_PI;
        for (uint sampleIndex = 0; sampleIndex < params.SpatialSamples; ++sampleIndex)
        {
            const float angle = angleOffset + float(sampleIndex) * kRestirGoldenAngle;
            const float radius = params.SpatialRadius *
                sqrt((float(sampleIndex) + 0.5f) / float(params.SpatialSamples));
            const int2 neighbor = pixel +
                int2(round(float2(cos(angle), sin(angle)) * radius));
            if (all(neighbor == pixel) || any(neighbor < int2(0, 0)) ||
                uint(neighbor.x) >= params.ReservoirWidth ||
                uint(neighbor.y) >= params.ReservoirHeight)
            {
                continue;
            }
            if (RestirFetchPrimitiveIndex(objectId0[neighbor]) != centerPrim)
            {
                continue;
            }
            if (bsdfDataBuffer[neighbor].x != packedBsdf.x)
            {
                continue;
            }
            const float3 neighborNormal = normalBuffer[neighbor].rgb;
            if (dot(neighborNormal, normal) < 0.9f)
            {
                continue;
            }
            const float3 neighborView = RestirReconstructViewPosition(
                camera, neighbor, size, depthBuffer[neighbor]);
            if (abs(neighborView.z - centerView.z) >
                0.1f * max(abs(centerView.z), 1e-3f))
            {
                continue;
            }

            FRestirState other = RestirUnpackReservoir(
                RestirGetIntermediateBuffer(params)[RestirReservoirIndex(neighbor, params)]);
            if (other.SampleType != RESTIR_SAMPLE_AREA_LIGHT ||
                other.LightIndex >= camera.LightCount)
            {
                continue;
            }
            other.M = min(other.M, float(params.InitialCandidates) * 4.0f);
            float3 otherF;
            float3 otherLightPosition;
            float otherTargetLum = 0.0f;
            if (Common.EvaluateAreaLightDiffuseSample(
                    other.LightIndex, other.LightUV, context,
                    otherF, otherLightPosition))
            {
                otherTargetLum = dot(otherF, kRestirLumaWeights);
            }
            combined.Merge(other, otherTargetLum, RandomFloat(randomSeed));
        }
    }
    combined.FinalizeW();

    float3 direct = float3(0, 0, 0);
    if (combined.SampleType == RESTIR_SAMPLE_AREA_LIGHT && combined.WeightW > 0.0f)
    {
        float3 f;
        float3 lightPosition;
        if (!Common.EvaluateAreaLightDiffuseSample(
                combined.LightIndex, combined.LightUV, context, f, lightPosition))
        {
            combined.WeightW = 0.0f;
        }
        else
        {
            const bool alreadyVerified = center.WeightW > 0.0f &&
                combined.LightIndex == center.LightIndex &&
                all(combined.LightUV == center.LightUV);
            const float3 shadowOrigin = tracer.GetAreaLightShadowOrigin(
                context.Position, context.Normal, lightPosition, reconstructionOffset);
            if (!alreadyVerified &&
                tracer.TraceAreaLightSegment(shadowOrigin, lightPosition, EPS))
            {
                combined.WeightW = 0.0f;
            }
            else
            {
                direct = f * combined.WeightW;
            }
        }
    }

    if (params.DebugMode != 0)
    {
        singleDiffuse[pixel] = RestirDebugColor(combined, params);
        return;
    }
    singleDiffuse[pixel] = singleDiffuse[pixel] + float4(direct, 0.0f);
}
