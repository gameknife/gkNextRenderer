import Shader.Bindless;
import Shader.PathTracing;
import Shader.VisibilityBuffer;
#include "common/PreProcessor.slang"
#include "common/NoAmbientShading.slang"

// Legacy monolithic SoftwareModernNoAmbient entry: decodes the visibility buffer inline, writes the
// G-buffer and lights the pixel in one dispatch. Kept as the rollback path for r.surface.build=0;
// Core.SwModernNoAmbientSurface is the same lighting kernel fed from the Primary Surface instead.
//
// The lighting itself lives in common/NoAmbientShading.slang so the two entries cannot drift.

void StoreBackground(int2 pixel, uint2 size, float3 primaryRayDir)
{
    let Camera = Bindless.GetGpuscene().Camera[0];
    const float4 skyColor = VisibleBackground(primaryRayDir);
    Bindless.GetViewStorageTexture<float4>(Bindless.RT_SINGLE_DIFFUSE).Store(pixel, skyColor);
    Bindless.GetViewStorageTexture<float4>(Bindless.RT_AMBIENT).Store(pixel, float4(0));
    Bindless.GetViewStorageTexture<uint>(Bindless.RT_OBJECTID_0).Store(pixel, 0);
    Bindless.GetViewStorageTexture<float>(Bindless.RT_PREV_DEPTHBUFFER).Store(pixel, 1000);
    const float2 skyMotionPixels =
        Common.CalculateSkyMotionVector(Camera, primaryRayDir) * float2(size);
    Bindless.GetViewStorageTexture<float4>(Bindless.RT_MOTIONVECTOR).Store(
        pixel, float4(skyMotionPixels, 0, 0));
    Bindless.GetViewStorageTexture<float4>(Bindless.RT_NORMAL).Store(pixel, float4(0));
}

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    let Camera = Bindless.GetGpuscene().Camera[0];
    uint2 size = Bindless.GetViewStorageTextureDimensions<float4>(Bindless.RT_SINGLE_DIFFUSE);
    // Checkerboard rendering requires the Primary Surface path, which this rollback entry does
    // not use, so the shading pixel is the dispatch pixel.
    const uint2 shadingPixel = DTid.xy;
    int2 pixel = int2(shadingPixel);
    if (any(shadingPixel >= size))
    {
        return;
    }

    float2 uv = ((float2(pixel) + 0.5f) / float2(size)) * 2.0 - 1.0;
    float4 origin = mul(Camera.ModelViewInverse, float4(0, 0, 0, 1));
    float4 target = mul(Camera.ProjectionInverse, float4(uv.x, uv.y, 1, 1));
    float4 rayDir4 = mul(Camera.ModelViewInverse, float4(normalize(target.xyz), 0));
    float3 primaryRayDir = normalize(rayDir4.xyz);

    uint2 vBuffer = Visibility.Load(pixel, Bindless.GetGpuscene().CustomData0);
    if (!Visibility.IsValid(vBuffer))
    {
        StoreBackground(pixel, size, primaryRayDir);
        return;
    }

    GPUVertex* vertices = Bindless.GetGpuscene().Vertices;
    uint* indices = Bindless.GetGpuscene().Indices;
    NodeProxy* nodes = Bindless.GetGpuscene().Nodes;
    ModelData* models = Bindless.GetGpuscene().Offsets;

    Vertex hitVertex;
    if (!Common.get_material_data(
            vBuffer, origin.xyz, primaryRayDir, nodes, models, vertices, indices, hitVertex))
    {
        // A stale or numerically degenerate visibility sample must not feed invalid
        // attributes into the G-buffer. Treat it as an uncovered sample.
        StoreBackground(pixel, size, primaryRayDir);
        return;
    }

    NodeProxy hitNode = nodes[vBuffer.x - 1];
    hitVertex.MaterialIndex = FetchMaterialId(hitNode, hitVertex.MaterialIndex);

    Material material = Bindless.GetGpuscene().GetMaterial(hitVertex.MaterialIndex);
    float4 primaryAlbedo = float4(1, 1, 1, 1);
    float4 gbuffer;
    Common.FetchGBuffer(hitVertex, material, hitNode, primaryRayDir, primaryAlbedo, gbuffer);

    float metalness = saturate(material.Metalness);
    if (material.MRATextureId >= 0)
    {
        float4 mra = Bindless.GetSampleTexture(material.MRATextureId).SampleLevel(hitVertex.TexCoord, 0);
        metalness = saturate(metalness * mra.b);
    }
    if (material.MaterialModel == MaterialMetallic)
    {
        metalness = 1.0f;
    }
    if (material.MaterialModel == MaterialDielectric)
    {
        metalness = 0.0f;
    }

    FNoAmbientShadingInput shadingInput;
    shadingInput.Position = hitVertex.Position;
    shadingInput.Normal = gbuffer.xyz;
    shadingInput.Roughness = gbuffer.a;
    shadingInput.Metalness = metalness;
    shadingInput.Albedo = primaryAlbedo;
    shadingInput.MaterialId = hitVertex.MaterialIndex;
    // The rollback path still runs GTAO after shading, so occlusion is applied by
    // Process.GTAOCompose instead of here.
    shadingInput.SkyOcclusion = 1.0f;
    const FNoAmbientShadingOutput lighting = ShadeNoAmbientSurface(shadingInput, pixel);

    float4 clipPos = mul(Camera.ViewProjection, float4(hitVertex.Position, 1.0));
    float ndcDepth = clipPos.z / clipPos.w;
    float2 motion = Common.CalculateMotionVector(Camera, hitNode, hitVertex, pixel) * float2(size);
    const bool selected = hitNode.reserved1 != 0;
    const bool hovered = (hitNode.reserved2 & 1u) != 0;
    const bool locked = (hitNode.reserved2 & 2u) != 0;
    const bool danger = (hitNode.reserved2 & 4u) != 0;
    Bindless.GetViewStorageTexture<float4>(Bindless.RT_SINGLE_DIFFUSE).Store(pixel, lighting.Diffuse);
    Bindless.GetViewStorageTexture<float4>(Bindless.RT_AMBIENT).Store(pixel, lighting.Ambient);
    Bindless.GetViewStorageTexture<uint>(Bindless.RT_OBJECTID_0).Store(pixel, Common.EncodeObjectId(hitNode.instanceId, selected, hovered, locked, danger));
    Bindless.GetViewStorageTexture<float>(Bindless.RT_PREV_DEPTHBUFFER).Store(pixel, ndcDepth);
    Bindless.GetViewStorageTexture<float4>(Bindless.RT_MOTIONVECTOR).Store(pixel, float4(motion, 0, 0));
    Bindless.GetViewStorageTexture<float4>(Bindless.RT_NORMAL).Store(pixel, float4(normalize(hitVertex.Normal), 1));
}
