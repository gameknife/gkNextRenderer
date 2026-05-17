import Common;

struct VertexOutput
{
    float4 position : SV_Position;
};

[shader("vertex")]
VertexOutput main(uint vertexIndex: SV_VertexID, uint instanceIndex: SV_InstanceID, uint instanceBase: SV_StartInstanceLocation)
{
    VertexOutput output;
    uint absoluteInstanceIdx = instanceIndex + instanceBase;
    NodeProxy proxy = Bindless.GetGpuscene().Nodes[absoluteInstanceIdx];

    GPUVertex* currentVertices = Bindless.GetGpuscene().Vertices;
    if (proxy.skinId != 0xFFFFFFFF)
    {
        currentVertices = Bindless.GetGpuscene().SkinnedVertices;
    }

    GPUVertex packedVertex = currentVertices[Bindless.GetGpuscene().Reorders[vertexIndex]];
    output.position =
        mul(mul(Bindless.GetGpuscene().Camera[0].ViewProjection, proxy.worldTS), float4(packedVertex.Position_Tx.xyz, 1.0));
    output.position.z -= 0.000001f * output.position.w;
    return output;
}
