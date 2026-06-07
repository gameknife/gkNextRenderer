import Common;

struct VertexOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] nointerpolation uint primitive_index : PRIMITIVE_INDEX;
    [[vk::location(1)]] nointerpolation uint material_model : MATERIAL_MODEL;
};

[shader("vertex")]
VertexOutput main(uint vertexIndex: SV_VertexID)
{
    VertexOutput output;
    let scene = Bindless.GetGpuscene();

    SoftMeshShaderResources softMeshShader = scene.SoftMeshShader[0];
    uint* prims = (uint*)softMeshShader.Prim;
    uint prim = prims[vertexIndex / 3];
    uint corner = vertexIndex % 3;
    uint instanceIdx = (prim >> 17) & 0x7FFF;
    uint triangleIdx = prim & 0x1FFFF;

    NodeProxy proxy = scene.Nodes[instanceIdx - 1];
    ModelData model = scene.Offsets[proxy.modelId];

    GPUVertex* currentVertices = scene.Vertices;
    if (proxy.skinId != 0xFFFFFFFF)
    {
        currentVertices = scene.SkinnedVertices;
    }

    uint localVertex = scene.Indices[model.indexOffset + triangleIdx * 3 + corner];
    GPUVertex packedVertex = currentVertices[model.vertexOffset + localVertex];
    output.position = mul(mul(scene.Camera[0].ViewProjection, proxy.worldTS),
                          float4(packedVertex.Position_Tx.xyz, 1.0));

    Vertex vertex = UnpackVertex(packedVertex);
    const uint materialId = FetchMaterialId(proxy, vertex.MaterialIndex);
    output.material_model = scene.GetMaterial(materialId).MaterialModel;
    output.primitive_index = prim;
    return output;
}
