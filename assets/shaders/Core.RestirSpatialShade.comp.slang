import Shader.Bindless;
import Shader.PathTracing;
#include "common/PreProcessor.slang"
#include "common/ShaderClock.slang"

// ReSTIR DI pass 2: spatial reservoir reuse + final visibility + shading.
// Reads the intermediate reservoirs written by the gather step inside the main PT dispatch,
// merges geometrically similar neighbors in W-form, verifies the winner with one shadow
// ray, adds the direct lighting into RT_SINGLE_DIFFUSE and stores the post-visibility
// reservoir into the FINAL buffer (consumed by the next frame's temporal merge).
// Design: docs/designs/pathtracing-restir-design.md §3.2 / §3.4.

static const float kGoldenAngle = 2.39996323f;

// Depth reconstruction must mirror FVisibilityBufferRayCaster exactly: the primary ray uses
// ndc = (pixel / size) * 2 - 1 (NO half-pixel offset) with the same ProjectionInverse, so
// unprojecting that ndc at the stored depth lands back on the primary ray. A half-pixel
// mismatch here is centimeters at distance and makes every shadow ray self-occlude.
float3 ReconstructViewPosition(UniformBufferObject camera, int2 pixel, uint2 size, float depth)
{
    const float2 ndc = (float2(pixel) / float2(size)) * 2.0f - 1.0f;
    const float4 view = mul(camera.ProjectionInverse, float4(ndc, depth, 1.0f));
    const float safeW = abs(view.w) > 1e-6f ? view.w : (view.w >= 0.0f ? 1e-6f : -1e-6f);
    return view.xyz / safeW;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    const uint2 size = Bindless.GetViewStorageTextureDimensions<float4>(Bindless.RT_SINGLE_DIFFUSE);
    if (any(DTid.xy >= size))
    {
        return;
    }
    // Safety gates mirror the entry shaders: primary view only, resources must exist.
    if (!RestirIsAvailable() || Bindless.GetGpuscene().CustomData0 != 0)
    {
        return;
    }

    FRestirRuntimeParameters params = RestirGetRuntimeParameters();
    const int2 pixel = int2(DTid.xy);
    if (uint(pixel.x) >= params.ReservoirWidth || uint(pixel.y) >= params.ReservoirHeight)
    {
        return;
    }
    const uint reservoirIndex = RestirReservoirIndex(pixel, params);

    GPUScene gpuScene = Bindless.GetGpuscene();
    UniformBufferObject Camera = gpuScene.Camera[0];

    // All G-buffer inputs were written by the main PT dispatch (previous pass), so reading
    // them here is coherent — unlike inside the gather step.
    let ObjectId0 = Bindless.GetViewStorageTexture<uint>(Bindless.RT_OBJECTID_0);
    let NormalBuffer = Bindless.GetViewStorageTexture<float4>(Bindless.RT_NORMAL);
    let DepthBuffer = Bindless.GetViewStorageTexture<float>(Bindless.RT_PREV_DEPTHBUFFER);
    let SingleDiffuse = Bindless.GetViewStorageTexture<float4>(Bindless.RT_SINGLE_DIFFUSE);

    const uint centerObjectId = ObjectId0[pixel];
    const uint centerPrim = RestirFetchPrimitiveIndex(centerObjectId);
    FRestirState center = RestirUnpackReservoir(RestirGetIntermediateBuffer(params)[reservoirIndex]);

    // Sky / emitter pixels carry an empty intermediate reservoir; invalid-id pixels (sky)
    // are also caught here. Store an empty final so the next frame never merges stale data.
    if (centerPrim == RESTIR_OBJID_INVALID || center.SampleType == RESTIR_SAMPLE_NONE)
    {
        FRestirState empty = FRestirState();
        empty.M = center.M;
        RestirGetFinalBuffer(params)[reservoirIndex] = RestirPackReservoir(empty);
        if (params.DebugMode != 0)
        {
            SingleDiffuse[pixel] = RestirDebugColor(empty, params);
        }
        return;
    }

    // Reconstruct the primary surface. The eye-direction nudge mirrors PrimaryHit so p-hat,
    // shadow-ray origins and shading match what the gather pass computed.
    const float3 normal = NormalBuffer[pixel].rgb;
    const float centerDepth = DepthBuffer[pixel];
    const float3 centerView = ReconstructViewPosition(Camera, pixel, size, centerDepth);
    float3 position = mul(Camera.ModelViewInverse, float4(centerView, 1.0f)).xyz;
    const float3 eye = mul(Camera.ModelViewInverse, float4(0, 0, 0, 1)).xyz;
    position -= normalize(position - eye) * Camera.SceneEpsilonScale * 0.01f;

    uint4 randomSeed = InitRandomSeed(DTid.x, DTid.y, Camera.TotalFrames + 7777u);

    // Depth-proportional origin offset absorbs fp32 reconstruction rounding at range.
    FHardwareRayTracer tracer;
    const float originOffset = max(EPS2, abs(centerView.z) * 1e-4f);
    const float3 shadowOrigin = position + normalize(normal) * originOffset;

    // Verify the center winner FIRST (this is the temporal chain's final visibility, same
    // semantics the M2 inline path had): an occluded entrenched winner must lose its W both
    // for the spatial selection below and for the stored history, or it survives forever as
    // a stuck dark sample.
    if (center.SampleType == RESTIR_SAMPLE_AREA_LIGHT && center.WeightW > 0.0f)
    {
        float3 centerF;
        float3 centerLightPosition;
        if (!Common.EvaluateAreaLightSample(center.LightIndex, center.LightUV, position, normal,
                                            centerF, centerLightPosition) ||
            tracer.TraceAreaLightSegment(shadowOrigin, centerLightPosition, EPS))
        {
            center.WeightW = 0.0f;
        }
    }

    // The temporal history gets the post-visibility CENTER reservoir, never the spatial
    // combination: spatial reuse is biased at visibility boundaries, and feeding it back
    // into the temporal chain would compound that bias frame over frame. Spatial reuse
    // below only affects this frame's shading.
    RestirGetFinalBuffer(params)[reservoirIndex] = RestirPackReservoir(center);

    FRestirState combined = FRestirState();
    combined.Merge(center, center.TargetLum, RandomFloat(randomSeed));

    // Spatial reuse: golden-angle spiral neighbors, biased mode with geometry similarity
    // tests (same object, normals within ~25 degrees, view depth within 10%). Neighbor
    // samples are light-surface points, so reuse needs no Jacobian; their target function
    // is re-evaluated at the center surface before the W-form merge.
    if ((params.Flags & RESTIR_FLAG_SPATIAL_ENABLE) != 0 && params.SpatialSamples > 0)
    {
        const float angleOffset = RandomFloat(randomSeed) * 2.0f * M_PI;
        for (uint s = 0; s < params.SpatialSamples; ++s)
        {
            const float angle = angleOffset + float(s) * kGoldenAngle;
            const float radius = params.SpatialRadius * sqrt((float(s) + 0.5f) / float(params.SpatialSamples));
            const int2 neighbor = pixel + int2(round(float2(cos(angle), sin(angle)) * radius));
            if (all(neighbor == pixel) ||
                any(neighbor < int2(0, 0)) ||
                uint(neighbor.x) >= params.ReservoirWidth || uint(neighbor.y) >= params.ReservoirHeight)
            {
                continue;
            }
            if (RestirFetchPrimitiveIndex(ObjectId0[neighbor]) != centerPrim)
            {
                continue;
            }
            const float3 neighborNormal = NormalBuffer[neighbor].rgb;
            if (dot(neighborNormal, normal) < 0.9f)
            {
                continue;
            }
            const float3 neighborView = ReconstructViewPosition(Camera, neighbor, size, DepthBuffer[neighbor]);
            if (abs(neighborView.z - centerView.z) > 0.1f * max(abs(centerView.z), 1e-3f))
            {
                continue;
            }

            FRestirState other = RestirUnpackReservoir(
                RestirGetIntermediateBuffer(params)[RestirReservoirIndex(neighbor, params)]);
            if (other.SampleType != RESTIR_SAMPLE_AREA_LIGHT || other.LightIndex >= Camera.LightCount)
            {
                continue;
            }
            // Cap neighbor M so temporally entrenched neighbors cannot dominate selection:
            // their samples may be occluded at the center (visibility is not part of the
            // similarity tests), and an M-heavy mismatched winner is exactly what produces
            // the penumbra darkening of the biased combine.
            other.M = min(other.M, float(params.InitialCandidates) * 4.0f);
            float3 otherF;
            float3 otherLightPosition;
            float otherTargetLum = 0.0f;
            if (Common.EvaluateAreaLightSample(other.LightIndex, other.LightUV, position, normal,
                                              otherF, otherLightPosition))
            {
                otherTargetLum = dot(otherF, kRestirLumaWeights);
            }
            combined.Merge(other, otherTargetLum, RandomFloat(randomSeed));
        }
    }
    combined.FinalizeW();

    // Shading visibility on the combined winner; the center winner was already verified
    // above, so its shadow ray is skipped.
    float3 direct = float3(0, 0, 0);
    if (combined.SampleType == RESTIR_SAMPLE_AREA_LIGHT && combined.WeightW > 0.0f)
    {
        float3 f;
        float3 lightPosition;
        if (!Common.EvaluateAreaLightSample(combined.LightIndex, combined.LightUV, position, normal, f, lightPosition))
        {
            combined.WeightW = 0.0f;
        }
        else
        {
            const bool alreadyVerified = center.WeightW > 0.0f &&
                combined.LightIndex == center.LightIndex &&
                all(combined.LightUV == center.LightUV);
            if (!alreadyVerified && tracer.TraceAreaLightSegment(shadowOrigin, lightPosition, EPS))
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
        SingleDiffuse[pixel] = RestirDebugColor(combined, params);
        return;
    }
    SingleDiffuse[pixel] = SingleDiffuse[pixel] + float4(direct, 0.0f);
}
