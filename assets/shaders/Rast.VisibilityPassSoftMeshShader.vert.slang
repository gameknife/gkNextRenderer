import Shader.Bindless;
import Shader.Types;
import Shader.GpuScene;
import Shader.Math;

struct VertexOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] nointerpolation uint2 primitive_index : PRIMITIVE_INDEX;
    [[vk::location(1)]] nointerpolation uint material_model : MATERIAL_MODEL;
};

[shader("vertex")]
VertexOutput main(uint vertexIndex: SV_VertexID)
{
    VertexOutput output;
    let scene = Bindless.GetGpuscene();

    SoftMeshShaderResources softMeshShader = scene.SoftMeshShader[0];
    VisibilityId visibility =
        LoadExpandedPrimitive(softMeshShader.Prim, vertexIndex / 3, softMeshShader.PrimitiveWordCount);
    uint corner = vertexIndex % 3;
    uint instanceIdx = visibility.instanceIdx;
    uint triangleIdx = visibility.triangleIdx;

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
    output.primitive_index = EncodeVisibilityPhysical(visibility, softMeshShader.PrimitiveWordCount);
    return output;
}
