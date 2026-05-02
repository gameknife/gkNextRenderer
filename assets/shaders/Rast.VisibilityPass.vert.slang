import Common;

struct VertexOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] nointerpolation uint primitive_index : PRIMITIVE_INDEX;
    [[vk::location(1)]] nointerpolation uint material_model : MATERIAL_MODEL;
};

[shader("vertex")]
VertexOutput main(uint vertexIndex: SV_VertexID, uint baseVertex: SV_StartVertexLocation, uint instanceIndex: SV_InstanceID, uint instanceBase: SV_StartInstanceLocation)
{
    VertexOutput output;
    uint absouluteInstanceIdx = instanceIndex + instanceBase;
    NodeProxy proxy = Bindless.GetGpuscene().Nodes[absouluteInstanceIdx];

    half4* currentVerticesSimple = Bindless.GetGpuscene().VerticesSimple;
    if (proxy.skinId != 0xFFFFFFFF)
    {
        currentVerticesSimple = Bindless.GetGpuscene().SkinnedVerticesSimple;
    }

    half4 Position = currentVerticesSimple[Bindless.GetGpuscene().Reorders[vertexIndex]];
    output.position = mul(mul(Bindless.GetGpuscene().Camera[0].ViewProjection, proxy.worldTS), float4(Position.xyz, 1.0));

    GPUVertex* currentVertices = Bindless.GetGpuscene().Vertices;
    if (proxy.skinId != 0xFFFFFFFF)
    {
        currentVertices = Bindless.GetGpuscene().SkinnedVertices;
    }
    Vertex vertex = UnpackVertex(currentVertices[Bindless.GetGpuscene().Reorders[vertexIndex]]);
    const uint materialId = FetchMaterialId(proxy, vertex.MaterialIndex);
    output.material_model = Bindless.GetGpuscene().GetMaterial(materialId).MaterialModel;

    uint instanceIdx = absouluteInstanceIdx + 1;
    uint triangleIdx = vertexIndex - baseVertex;
    // we use a 32bit visibility buffer, with 1 bit shift to left, support up to 32768 instances and 131072 triangles per instance
    // if exceed, render result will be undefined
    output.primitive_index = ((instanceIdx & 0x7FFF) << 17) | (triangleIdx & 0x1FFFF);
    return output;
}
