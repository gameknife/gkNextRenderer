import Shader.Bindless;
import Shader.Types;
import Shader.GpuScene;
import Shader.Math;
import Shader.VisibilityBuffer;

struct VertexOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] nointerpolation uint2 visibility_id : VISIBILITY_ID;
    [[vk::location(1)]] nointerpolation uint material_model : MATERIAL_MODEL;
};

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

    // The level the cull pass chose travels in the id, so the triangle ordinal is resolved against
    // that level's index range rather than LOD0's.
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
    output.position = mul(mul(scene.Camera[0].ViewProjection, proxy.worldTS),
                          float4(packedVertex.Position_Tx.xyz, 1.0));

    Vertex vertex = UnpackVertex(packedVertex);
    const uint materialId = FetchMaterialId(proxy, vertex.MaterialIndex);
    output.material_model = scene.GetMaterial(materialId).MaterialModel;
    output.visibility_id = visibilityId;
    return output;
}
