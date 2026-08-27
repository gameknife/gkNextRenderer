import Shader.Bindless;
import Shader.Types;
import Shader.GpuScene;
import Shader.VisibilityBuffer;

struct VertexOutput
{
    float4 position : SV_Position;
};

[shader("vertex")]
VertexOutput main(uint vertexIndex: SV_VertexID)
{
    VertexOutput output;
    let scene = Bindless.GetGpuscene();

    SoftMeshShaderResources softMeshShader = scene.SoftMeshShader[0];
    uint2 visibilityId = Visibility.LoadExpandedPrimitive(softMeshShader.Prim, vertexIndex / 3);
    uint corner = vertexIndex % 3;

    NodeProxy proxy = scene.Nodes[visibilityId.x - 1];
    ModelData model = scene.Offsets[proxy.modelId];

    uint localVertex = scene.Indices[model.indexOffset + visibilityId.y * 3 + corner];
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
    output.position.z -= 0.000001f * output.position.w;
    return output;
}
