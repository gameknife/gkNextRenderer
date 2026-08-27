import Shader.Bindless;
import Shader.PathTracing;
import Shader.VisibilityBuffer;
import Shader.SurfaceBuffer;
#include "common/PreProcessor.slang"

// Primary Surface Build.
//
// Resolves the visibility buffer exactly once per pixel into the dense Primary Surface that GTAO,
// the screen-space consumers and the shading kernels all read. This pass breaks the dependency
// cycle the old architecture had - dense depth and normals used to be *outputs* of Core Shading,
// which is why every screen-space consumer had to re-decode the visibility buffer per march step.
//
// Build is deliberately unconditionally full-rate: checkerboard is a shading allocation strategy,
// and neighbour-copied depth / motion / object id is semantically wrong surface data at every
// silhouette and depth discontinuity. Only lighting is ever reconstructed.
//
// Build does no lighting, no ray traversal and no compose. Contract: Shader/SurfaceBuffer.slang.

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    let Camera = Bindless.GetGpuscene().Camera[0];
    const uint viewBase = Bindless.GetGpuscene().CustomData0;
    const bool writeSpecularAlbedo =
        (Bindless.GetGpuscene().CustomData1 & Surface.kBuildWriteSpecularAlbedo) != 0u;

    const uint2 size = Surface.Dimensions(viewBase);
    const int2 pixel = int2(DTid.xy);
    if (any(DTid.xy >= size))
    {
        return;
    }

    const float2 uv = ((float2(pixel) + 0.5f) / float2(size)) * 2.0 - 1.0;
    const float4 origin = mul(Camera.ModelViewInverse, float4(0, 0, 0, 1));
    const float4 target = mul(Camera.ProjectionInverse, float4(uv.x, uv.y, 1, 1));
    const float4 rayDir4 = mul(Camera.ModelViewInverse, float4(normalize(target.xyz), 0));
    const float3 primaryRayDir = normalize(rayDir4.xyz);

    Surface.FPrimarySurface surface;
    surface.Depth = Surface.kBackgroundDepth;
    surface.Normal = float3(0, 0, 0);
    surface.Roughness = 0.0f;
    surface.Albedo = float4(1, 1, 1, 1);
    surface.SpecularAlbedo = float4(0, 0, 0, 0);
    surface.ObjectId = 0u;
    surface.Motion = Common.CalculateSkyMotionVector(Camera, primaryRayDir) * float2(size);
    surface.MaterialId = Surface.kInvalidMaterialId;
    surface.Metalness = 0.0f;
    surface.Flags = 0u;

    GPUVertex* vertices = Bindless.GetGpuscene().Vertices;
    uint* indices = Bindless.GetGpuscene().Indices;
    NodeProxy* nodes = Bindless.GetGpuscene().Nodes;
    ModelData* models = Bindless.GetGpuscene().Offsets;

    const uint2 vBuffer = Visibility.Load(pixel, viewBase);
    Vertex hitVertex;
    // A stale or numerically degenerate visibility sample must not feed invalid attributes into the
    // surface. Treat it as an uncovered sample - the same rejection semantics the inline decoders
    // have always had, kept here so it exists in exactly one place.
    if (!Visibility.IsValid(vBuffer) ||
        !Common.get_material_data(
            vBuffer, origin.xyz, primaryRayDir, nodes, models, vertices, indices, hitVertex))
    {
        Surface.Store(viewBase, pixel, surface, writeSpecularAlbedo);
        return;
    }

    NodeProxy hitNode = nodes[vBuffer.x - 1];
    hitVertex.MaterialIndex = FetchMaterialId(hitNode, hitVertex.MaterialIndex);
    const Material material = Bindless.GetGpuscene().GetMaterial(hitVertex.MaterialIndex);

    float4 albedo = float4(1, 1, 1, 1);
    float4 gbuffer = float4(0, 0, 0, 0);
    Common.FetchGBuffer(hitVertex, material, hitNode, primaryRayDir, albedo, gbuffer);

    // Effective metalness, matching BuildBSDFContext exactly: MRA modulation first, then the
    // material-model override. Storing the effective value is what lets a consumer shade without
    // the primary TexCoord.
    float metalness = saturate(material.Metalness);
    if (material.MRATextureId >= 0)
    {
        const float4 mra =
            Bindless.GetSampleTexture(material.MRATextureId).SampleLevel(hitVertex.TexCoord, 0);
        metalness = saturate(metalness * mra.b);
    }
    if (material.MaterialModel == MaterialMetallic)
    {
        metalness = 1.0f;
    }
    else if (material.MaterialModel == MaterialDielectric)
    {
        metalness = 0.0f;
    }

    // Depth goes through the jittered ViewProjection so it inverts exactly with
    // Camera.ProjectionInverse, which is what every reconstruction in the engine uses.
    const float4 clipPos = mul(Camera.ViewProjection, float4(hitVertex.Position, 1.0));
    const bool selected = hitNode.reserved1 != 0;
    const bool hovered = (hitNode.reserved2 & 1u) != 0;
    const bool locked = (hitNode.reserved2 & 2u) != 0;
    const bool danger = (hitNode.reserved2 & 4u) != 0;

    surface.Depth = clipPos.z / clipPos.w;
    surface.Normal = normalize(gbuffer.xyz);
    surface.Roughness = gbuffer.a;
    surface.Albedo = albedo;
    surface.SpecularAlbedo =
        material.MaterialModel == MaterialMetallic ? albedo : float4(1, 1, 1, 1);
    surface.ObjectId =
        Common.EncodeObjectId(hitNode.instanceId, selected, hovered, locked, danger);
    // CalculateMotionVector also owns the RT_MOTIONMOMENT read-modify-write; that side effect
    // belongs to whoever owns motion, which is now Build.
    surface.Motion =
        Common.CalculateMotionVector(Camera, hitNode, hitVertex, pixel) * float2(size);
    surface.MaterialId = hitVertex.MaterialIndex;
    surface.Metalness = metalness;
    surface.Flags = Common.MakeSurfaceFlags(material);

    // An emitter has no meaningful specular albedo; the tracing renderers zero it explicitly.
    if (material.MaterialModel == MaterialDiffuseLight)
    {
        surface.SpecularAlbedo = float4(0, 0, 0, 0);
    }

    Surface.Store(viewBase, pixel, surface, writeSpecularAlbedo);
}
