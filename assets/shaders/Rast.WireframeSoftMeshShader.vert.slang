import Shader.Bindless;
import Shader.Types;
import Shader.GpuScene;
import Shader.VisibilityBuffer;

// Wireframe reads the same expanded primitive stream the visibility pass rasterises, so a single
// indirect draw covers the whole scene and what it shows is exactly what survived culling: a proxy
// removed by the frustum or occlusion test simply contributes no primitives here. The LOD level
// rides in the visibility id, so the level each proxy was drawn at is available for free.
//
// Colour is resolved here rather than in the fragment stage. That is cheaper (per-vertex instead of
// per-fragment, and these are lines), and it keeps the interface to one interpolant. An earlier
// version branched on the push constant in the fragment stage and, on this MoltenVK/Metal target,
// consistently took the wrong branch even though a disassembly showed it reading the correct
// member at the correct offset; deciding in the vertex stage sidesteps that entirely.
static const uint kColorModeByLod = 1u;

struct VertexOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] nointerpolation float3 color : WIRE_COLOR;
};

// Level 0 reads cool/green and each coarser level warms up, so how much red is on screen is a
// direct read on where LOD is taking detail away.
float3 LodLevelColor(uint level)
{
    switch (level)
    {
    case 0u:  return float3(0.25f, 1.00f, 0.55f);
    case 1u:  return float3(0.95f, 0.95f, 0.30f);
    case 2u:  return float3(1.00f, 0.55f, 0.15f);
    default:  return float3(1.00f, 0.25f, 0.25f);
    }
}

[shader("vertex")]
VertexOutput main(uint vertexIndex: SV_VertexID)
{
    VertexOutput output;
    let scene = Bindless.GetGpuscene();

    SoftMeshShaderResources softMeshShader = scene.SoftMeshShader[0];
    uint2 visibilityId = Visibility.LoadExpandedPrimitive(softMeshShader.Prim, vertexIndex / 3);
    uint corner = vertexIndex % 3;

    NodeProxy proxy = scene.Nodes[Visibility.ProxyIndex(visibilityId)];
    ModelData model = scene.Offsets[proxy.modelId];

    uint localVertex = scene.Indices[model.lodIndexOffset[Visibility.LodLevel(visibilityId)] + visibilityId.y * 3 + corner];
    uint vertexAddress = model.vertexOffset + localVertex;
    GPUVertex packedVertex;
    if (proxy.skinId != 0xFFFFFFFF)
    {
        packedVertex = scene.SkinnedVertices[vertexAddress];
    }
    else
    {
        packedVertex = scene.Vertices[vertexAddress];
    }
    output.position =
        mul(mul(scene.Camera[0].ViewProjection, proxy.worldTS), float4(packedVertex.Position_Tx.xyz, 1.0));
    // Reverse-Z: nudge toward the camera so an edge lying exactly on the surface it belongs to wins
    // the depth test instead of z-fighting with it. Irrelevant when the pass runs depth-test-off.
    output.position.z += 0.000001f * output.position.w;

    // CustomData1 carries the colour mode; this is the wireframe pass's own copy of GPUScene, so
    // borrowing that word cannot disturb any other pass.
    output.color = scene.CustomData1 == kColorModeByLod
        ? LodLevelColor(Visibility.LodLevel(visibilityId))
        // Bright cyan holds up against both lit surfaces and a dark background. The original
        // 10%-black lines were effectively invisible on anything but a white surface.
        : float3(0.20f, 0.95f, 1.00f);
    return output;
}
