import Shader.Bindless;
import Shader.Types;
import Shader.GpuScene;

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
    output.position =
        mul(mul(scene.Camera[0].ViewProjection, proxy.worldTS), float4(packedVertex.Position_Tx.xyz, 1.0));
    output.position.z -= 0.000001f * output.position.w;
    return output;
}
