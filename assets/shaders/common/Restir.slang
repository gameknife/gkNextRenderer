// ReSTIR DI shared primitives: availability, reservoir pack/unpack, streaming RIS
// (Update / Merge / FinalizeW) and the M0 plumbing debug views. Data layouts live in
// BasicTypes.slang (FPathTracingExtras, FRestirReservoir, FRestirRuntimeParameters);
// buffer ownership and per-frame parameters are in PathTracingRenderer.cpp.
// Design: docs/designs/pathtracing-restir-design.md

public static const uint RESTIR_FLAG_TEMPORAL_ENABLE = 1u << 0;
public static const uint RESTIR_FLAG_SPATIAL_ENABLE = 1u << 1;
public static const uint RESTIR_FLAG_TEMPORAL_VALID = 1u << 2;

public static const uint RESTIR_SAMPLE_NONE = 0u;
public static const uint RESTIR_SAMPLE_AREA_LIGHT = 1u;

// Explicit branch on purpose: scalar && does not guarantee short-circuiting, and the field
// load must never execute against a null table pointer (see SharcIsAvailable).
public bool RestirIsAvailable()
{
    FPathTracingExtras* extras = Bindless.GetGpuscene().PtExtras;
    if (extras == (FPathTracingExtras*)0)
    {
        return false;
    }
    return extras[0].RestirParameters != 0;
}

public FRestirRuntimeParameters RestirGetRuntimeParameters()
{
    FPathTracingExtras* extras = Bindless.GetGpuscene().PtExtras;
    return ((FRestirRuntimeParameters*)extras[0].RestirParameters)[0];
}

// Temporal validity rides the recorded push constant (gpuScene.CustomData1, stamped by
// PathTracingRenderer per dispatch) instead of the host-visible parameter buffer: the host
// rewrites that buffer every frame while previous frames are still in flight.
public static const uint RESTIR_FRAME_TEMPORAL_VALID_BIT = 1u << 1;

// Fixed-role double buffer (no frame parity). The intermediate buffer is written by the
// gather pass (initial RIS + temporal merge, inside the main PT dispatch) and read by the
// spatial/shade pass; the final buffer is written by the spatial/shade pass
// (post-visibility) and read by the NEXT frame's temporal merge. Each buffer has exactly
// one writer pass, so cross-pixel neighbor reads never observe partially updated data.
public FRestirReservoir* RestirGetIntermediateBuffer(FRestirRuntimeParameters params)
{
    return (FRestirReservoir*)Bindless.GetGpuscene().PtExtras[0].RestirReservoirPing;
}

public FRestirReservoir* RestirGetFinalBuffer(FRestirRuntimeParameters params)
{
    return (FRestirReservoir*)Bindless.GetGpuscene().PtExtras[0].RestirReservoirPong;
}

public uint RestirReservoirIndex(int2 pixel, FRestirRuntimeParameters params)
{
    return uint(pixel.y) * params.ReservoirWidth + uint(pixel.x);
}

// Unpacked reservoir working state for streaming RIS. WeightSum only lives inside a pass;
// across frames the reservoir round-trips (sample, M, TargetLum, WeightW) and merges use
// the W-form candidate weight, so WeightSum is not reconstructed on unpack.
public struct FRestirState
{
    public uint LightIndex;
    public uint SampleType;
    public float2 LightUV;
    public float WeightSum;
    public float M;
    public float TargetLum;
    public float WeightW;

    public __init()
    {
        LightIndex = 0;
        SampleType = RESTIR_SAMPLE_NONE;
        LightUV = float2(0, 0);
        WeightSum = 0;
        M = 0;
        TargetLum = 0;
        WeightW = 0;
    }

    // Stream one fresh candidate with RIS weight w = targetLum / sourcePdf.
    // Returns true when this candidate became the reservoir sample.
    [mutating] public bool Update(uint lightIndex, float2 uv, float risWeight, float targetLum, float random)
    {
        WeightSum += risWeight;
        M += 1.0f;
        if (WeightSum > 0.0f && risWeight > 0.0f && random * WeightSum <= risWeight)
        {
            LightIndex = lightIndex;
            SampleType = RESTIR_SAMPLE_AREA_LIGHT;
            LightUV = uv;
            TargetLum = targetLum;
            return true;
        }
        return false;
    }

    // Merge another reservoir whose target function has been re-evaluated at this pixel
    // (targetLumAtPixel). Candidate weight uses the W form: w = p-hat * W * M.
    [mutating] public bool Merge(FRestirState other, float targetLumAtPixel, float random)
    {
        const float risWeight = targetLumAtPixel * other.WeightW * other.M;
        WeightSum += risWeight;
        M += other.M;
        if (WeightSum > 0.0f && risWeight > 0.0f && random * WeightSum <= risWeight)
        {
            LightIndex = other.LightIndex;
            SampleType = other.SampleType;
            LightUV = other.LightUV;
            TargetLum = targetLumAtPixel;
            return true;
        }
        return false;
    }

    // W = (1 / p-hat(y)) * (WeightSum / M). Call once after all Update/Merge streaming.
    [mutating] public void FinalizeW()
    {
        const float denom = TargetLum * max(M, 1.0f);
        WeightW = denom > 1e-8f ? WeightSum / denom : 0.0f;
        if (isnan(WeightW) || isinf(WeightW))
        {
            WeightW = 0.0f;
        }
    }
}

public FRestirReservoir RestirPackReservoir(FRestirState state)
{
    FRestirReservoir packed;
    packed.LightData = (state.LightIndex & 0x00FFFFFFu) | ((state.SampleType & 0xFFu) << 24);
    const float2 uv = saturate(state.LightUV);
    packed.PackedUV = uint(uv.x * 65535.0f + 0.5f) | (uint(uv.y * 65535.0f + 0.5f) << 16);
    packed.WeightW = state.WeightW;
    const uint m = min(uint(state.M + 0.5f), 0xFFFFu);
    packed.PackedMTarget = m | (f32tof16(state.TargetLum) << 16);
    return packed;
}

public FRestirState RestirUnpackReservoir(FRestirReservoir packed)
{
    FRestirState state = FRestirState();
    state.LightIndex = packed.LightData & 0x00FFFFFFu;
    state.SampleType = packed.LightData >> 24;
    state.LightUV = float2(float(packed.PackedUV & 0xFFFFu), float(packed.PackedUV >> 16)) / 65535.0f;
    state.WeightW = packed.WeightW;
    state.M = float(packed.PackedMTarget & 0xFFFFu);
    state.TargetLum = f16tof32(packed.PackedMTarget >> 16);
    return state;
}

public static const float3 kRestirLumaWeights = float3(0.212671f, 0.715160f, 0.072169f);

// Object-id decoding mirrors Process.ReProject so temporal rejection semantics match the
// color-history reprojection exactly.
public static const uint RESTIR_OBJID_FLAG_VALID = 0x80000000u;
public static const uint RESTIR_OBJID_INSTANCE_MASK = 0x07FFFFFFu;
public static const uint RESTIR_OBJID_INVALID = 0xFFFFFFFFu;

public uint RestirFetchPrimitiveIndex(uint objectId)
{
    return (objectId & RESTIR_OBJID_FLAG_VALID) != 0 ? (objectId & RESTIR_OBJID_INSTANCE_MASK)
                                                     : RESTIR_OBJID_INVALID;
}

// Store an empty intermediate reservoir (primary-ray miss / emitter pixels, where the
// gather path does not run) so the spatial pass never consumes stale data there.
public void RestirStoreEmpty(int2 pixel)
{
    FRestirRuntimeParameters params = RestirGetRuntimeParameters();
    if (uint(pixel.x) >= params.ReservoirWidth || uint(pixel.y) >= params.ReservoirHeight)
    {
        return;
    }
    RestirGetIntermediateBuffer(params)[RestirReservoirIndex(pixel, params)] =
        RestirPackReservoir(FRestirState());
}

// Gather pass (runs inside the main PT dispatch): initial candidates via streaming RIS,
// initial-winner visibility, temporal merge from the previous frame's FINAL reservoirs,
// then stores the post-temporal reservoir to the INTERMEDIATE buffer. Shading and spatial
// reuse happen in Core.RestirSpatialShade.comp.slang, which also owns the final visibility
// ray and writes the final buffer. Sampling shares Common.EvaluateAreaLightSample with
// classic NEE so the target function and shading can never drift apart.
public void RestirPrimaryGather(inout uint4 randomSeed, int2 pixel, float3 position, float3 normal,
                                uint primaryInstanceId, float2 motionPixels, FHardwareRayTracer tracer)
{
    // Defense in depth: never touch the parameter table or reservoirs unless the resources
    // actually exist, whatever the caller's gating did.
    if (!RestirIsAvailable())
    {
        return;
    }

    GPUScene gpuScene = Bindless.GetGpuscene();
    UniformBufferObject Camera = gpuScene.Camera[0];
    FRestirRuntimeParameters params = RestirGetRuntimeParameters();

    if (uint(pixel.x) >= params.ReservoirWidth || uint(pixel.y) >= params.ReservoirHeight)
    {
        return;
    }
    const uint reservoirIndex = RestirReservoirIndex(pixel, params);

    FRestirState reservoir = FRestirState();
    if (Camera.LightCount == 0 || gpuScene.SoftMeshShader.Lights == 0)
    {
        RestirGetIntermediateBuffer(params)[reservoirIndex] = RestirPackReservoir(reservoir);
        return;
    }

    // Initial candidates: CDF light selection + uniform uv on the light quad, streamed with
    // RIS weight w = p-hat / p-source where p-source = lightPdf / area. Invalid candidates
    // still count toward M (equivalent to streaming them with zero weight).
    for (uint c = 0; c < params.InitialCandidates; ++c)
    {
        const uint lightIndex = Common.SelectAreaLight(RandomFloat(randomSeed));
        LightObject light = gpuScene.Lights[lightIndex];
        const float lightPdf = asfloat(light.reserved2);
        const float lightArea = light.normal_area.w;
        const float2 uv = RandomFloat2(randomSeed);
        float3 f;
        float3 lightPosition;
        if (lightPdf <= 0.0f || lightArea <= 0.0f ||
            !Common.EvaluateAreaLightSample(lightIndex, uv, position, normal, f, lightPosition))
        {
            reservoir.M += 1.0f;
            continue;
        }
        const float targetLum = dot(f, kRestirLumaWeights);
        const float risWeight = targetLum * (lightArea / lightPdf);
        reservoir.Update(lightIndex, uv, risWeight, targetLum, RandomFloat(randomSeed));
    }
    reservoir.FinalizeW();

    // Initial-winner visibility (visibility reuse): an occluded winner keeps its M but drops
    // W to zero, so the W-form merges below never spread shadowed samples as light leaks.
    if (reservoir.SampleType == RESTIR_SAMPLE_AREA_LIGHT && reservoir.WeightW > 0.0f)
    {
        const float3 shadowOrigin = position + normalize(normal) * EPS2;
        float3 f;
        float3 lightPosition;
        if (!Common.EvaluateAreaLightSample(reservoir.LightIndex, reservoir.LightUV, position, normal, f, lightPosition) ||
            tracer.TraceAreaLightSegment(shadowOrigin, lightPosition, EPS))
        {
            reservoir.WeightW = 0.0f;
        }
    }

    // Combine in W-form (candidate weight = p-hat * W * M): a visibility-zeroed reservoir
    // contributes only M, never selection weight.
    FRestirState combined = FRestirState();
    combined.Merge(reservoir, reservoir.TargetLum, RandomFloat(randomSeed));

    // Temporal reuse: reproject via the motion vector into the previous frame's FINAL
    // reservoirs (post-spatial, post-visibility). Rejection mirrors Process.ReProject:
    // off-screen, object-id mismatch and moving-object (MotionMoment) pixels drop history.
    // The history sample's target function is re-evaluated at this pixel before merging; an
    // invalid-here sample still contributes M (biased-mode accounting, per design).
    if ((params.Flags & RESTIR_FLAG_TEMPORAL_ENABLE) != 0 &&
        (Bindless.GetGpuscene().CustomData1 & RESTIR_FRAME_TEMPORAL_VALID_BIT) != 0)
    {
        // Current-pixel primary data (instance id, motion) comes from the renderer state:
        // this invocation just wrote those textures and reading them back is not coherent.
        // ObjectId1 and MotionMoment were written by previous passes, so reading them is safe.
        let ObjectId1 = Bindless.GetViewStorageTexture<uint>(Bindless.RT_OBJECTID_1);
        let MotionMoment = Bindless.GetViewStorageTexture<uint>(Bindless.RT_MOTIONMOMENT);

        const int2 prevPixel = int2(floor(float2(pixel) + motionPixels));
        const bool inside = all(prevPixel >= int2(0, 0)) &&
                            uint(prevPixel.x) < params.ReservoirWidth &&
                            uint(prevPixel.y) < params.ReservoirHeight;
        if (inside && MotionMoment[pixel] == 0)
        {
            const uint currPrim = primaryInstanceId & RESTIR_OBJID_INSTANCE_MASK;
            const uint prevPrim = RestirFetchPrimitiveIndex(ObjectId1[prevPixel]);
            if (currPrim == prevPrim)
            {
                FRestirState prev = RestirUnpackReservoir(
                    RestirGetFinalBuffer(params)[RestirReservoirIndex(prevPixel, params)]);
                if (prev.SampleType == RESTIR_SAMPLE_AREA_LIGHT && prev.LightIndex < Camera.LightCount)
                {
                    prev.M = min(prev.M, float(params.TemporalMClamp));
                    float3 prevF;
                    float3 prevLightPosition;
                    float prevTargetLum = 0.0f;
                    if (Common.EvaluateAreaLightSample(prev.LightIndex, prev.LightUV, position, normal,
                                                      prevF, prevLightPosition))
                    {
                        prevTargetLum = dot(prevF, kRestirLumaWeights);
                    }
                    combined.Merge(prev, prevTargetLum, RandomFloat(randomSeed));
                }
            }
        }
    }
    combined.FinalizeW();

    RestirGetIntermediateBuffer(params)[reservoirIndex] = RestirPackReservoir(combined);
}

// Debug color for the spatial/shade pass, built from the locally combined reservoir state
// (race-free; the final buffer is being rewritten concurrently by other pixels). Colors
// follow the residency-debug convention (~0..255 display nits).
// 1 = M heatmap, 2 = W, 3 = winner light index, 4 = temporal/spatial reuse proxy.
public float4 RestirDebugColor(FRestirState state, FRestirRuntimeParameters params)
{
    if (params.DebugMode == 2u)
    {
        const float w = saturate(state.WeightW * 0.25f);
        return float4(w * 255.0f, w * 255.0f, 0, 1);
    }
    if (params.DebugMode == 3u)
    {
        if (state.SampleType == RESTIR_SAMPLE_NONE)
        {
            return float4(0, 0, 0, 1);
        }
        const uint h = state.LightIndex * 2654435761u;
        return float4(float(h & 0xFFu), float((h >> 8) & 0xFFu), float((h >> 16) & 0xFFu), 1);
    }
    if (params.DebugMode == 4u)
    {
        // Reuse proxy: M above the initial candidate count means history or neighbors
        // merged this frame. Red = no reuse, green intensity = accumulation toward clamp.
        if (state.SampleType == RESTIR_SAMPLE_NONE)
        {
            return float4(0, 0, 0, 1);
        }
        const float extra = max(state.M - float(params.InitialCandidates), 0.0f);
        if (extra < 0.5f)
        {
            return float4(255.0f, 0, 0, 1);
        }
        const float t = saturate(extra / max(float(params.TemporalMClamp) - float(params.InitialCandidates), 1.0f));
        return float4(0, 255.0f * max(t, 0.15f), 0, 1);
    }
    const float m = saturate(state.M / max(float(params.TemporalMClamp), 1.0f));
    const float occupied = state.SampleType != RESTIR_SAMPLE_NONE ? 51.0f : 0.0f;
    return float4(255.0f * m, 255.0f * (1.0f - m), occupied, 1);
}
