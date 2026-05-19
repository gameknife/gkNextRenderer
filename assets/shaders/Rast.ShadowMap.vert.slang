import Common;
import Bindless;

struct VertexOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] nointerpolation uint material_model : MATERIAL_MODEL;
};

[shader("vertex")]
VertexOutput main(uint vertexIndex: SV_VertexID, uint baseVertex: SV_StartVertexLocation,
                  uint instanceIndex: SV_InstanceID, uint instanceBase: SV_StartInstanceLocation)
{
    VertexOutput output;
    uint absouluteInstanceIdx = instanceIndex + instanceBase;
    let GpuScene = Bindless.GetGpuscene();
    NodeProxy proxy = GpuScene.Nodes[absouluteInstanceIdx];

    GPUVertex* currentVertices = GpuScene.Vertices;
    if (proxy.skinId != 0xFFFFFFFF)
    {
        currentVertices = GpuScene.SkinnedVertices;
    }
    GPUVertex packedVertex = currentVertices[GpuScene.Reorders[vertexIndex]];

    uint cascadeIdx = min(GpuScene.custom_data_0, 3u);
    float4x4 lightVP = GpuScene.Camera[0].SunCascadeViewProjection[cascadeIdx];
    output.position = mul(mul(lightVP, proxy.worldTS), float4(packedVertex.Position_Tx.xyz, 1.0));

    Vertex vertex = UnpackVertex(packedVertex);
    const uint materialId = FetchMaterialId(proxy, vertex.MaterialIndex);
    output.material_model = GpuScene.GetMaterial(materialId).MaterialModel;
    return output;
}
