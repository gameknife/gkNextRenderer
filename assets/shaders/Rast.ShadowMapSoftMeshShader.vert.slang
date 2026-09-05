import Shader.Bindless;
import Shader.Types;
import Shader.GpuScene;
import Shader.Math;
import Shader.VisibilityBuffer;

struct VertexOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] nointerpolation uint material_model : MATERIAL_MODEL;
};

[shader("vertex")]
VertexOutput main(uint vertexIndex: SV_VertexID)
{
    VertexOutput output;
    let scene = Bindless.GetGpuscene();

    uint cascadeIdx = min(scene.CustomData0, 3u);
    uint slot = cascadeIdx + 1;

    SoftMeshShaderResources softMeshShader = scene.SoftMeshShader[0];
    uint2 visibilityId = Visibility.LoadExpandedPrimitive(
        softMeshShader.ShadowPrim, scene.CustomData2 + vertexIndex / 3);
    uint corner = vertexIndex % 3;

    NodeProxy proxy = scene.Nodes[Visibility.ProxyIndex(visibilityId)];
    ModelData model = scene.Offsets[proxy.modelId];

    // Shadow cull picks a level per cascade, so this resolves against whatever level this
    // cascade's cull pass reserved triangles for.
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

    float4x4 lightVP = scene.Camera[0].SunCascadeViewProjection[cascadeIdx];
    output.position = mul(mul(lightVP, proxy.worldTS), float4(packedVertex.Position_Tx.xyz, 1.0));

    Vertex vertex = UnpackVertex(packedVertex);
    const uint materialId = FetchMaterialId(proxy, vertex.MaterialIndex);
    output.material_model = scene.GetMaterial(materialId).MaterialModel;
    return output;
}
