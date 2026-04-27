import Common;
import Bindless;

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    GPUScene scene = Bindless.GetGpuscene();
    uint nodeIdx = scene.custom_data_0;
    uint vertexOffset = scene.custom_data_1;
    uint vertexCount = scene.custom_data_2;

    if (DTid.x >= vertexCount) return;

    uint vertexIdx = vertexOffset + DTid.x;
    NodeProxy node = scene.Nodes[nodeIdx];
    
    // Get original vertex
    GPUVertex baseVertex = scene.Vertices[vertexIdx];
    
    // Unpack positions, normals, tangents
    Vertex v = UnpackVertex(baseVertex);
    
    // Load weights and joints
    float4 weights = scene.SkinWeights[vertexIdx];
    uint4 joints = scene.SkinJoints[vertexIdx];
    
    float4x4 skinMatrix = 
        weights.x * scene.JointMatrices[node.jointMatrixOffset + joints.x] +
        weights.y * scene.JointMatrices[node.jointMatrixOffset + joints.y] +
        weights.z * scene.JointMatrices[node.jointMatrixOffset + joints.z] +
        weights.w * scene.JointMatrices[node.jointMatrixOffset + joints.w];
    
    // Transform position and normal (using float4x4 skinMatrix)
    float3 skinnedPos = mul(skinMatrix, float4(v.Position, 1.0)).xyz;
    // For normals, we should ideally use inverse transpose, but for LBS with orthogonal matrices, 3x3 is fine.
    float3 skinnedNormal = mul((float3x3)skinMatrix, v.Normal);
    float3 skinnedTangent = mul((float3x3)skinMatrix, v.Tangent.xyz);
    
    // Pack back to GPUVertex
    GPUVertex outVertex;
    outVertex.Position_Tx = half4((half)skinnedPos.x, (half)skinnedPos.y, (half)skinnedPos.z, baseVertex.Position_Tx.w);
    outVertex.Normal_Ty   = half4((half)skinnedNormal.x, (half)skinnedNormal.y, (half)skinnedNormal.z, baseVertex.Normal_Ty.w);
    outVertex.Tangent     = half4((half)skinnedTangent.x, (half)skinnedTangent.y, (half)skinnedTangent.z, baseVertex.Tangent.w);
    
    // Write to SkinnedVertices
    scene.SkinnedVertices[vertexIdx] = outVertex;
    scene.SkinnedVerticesSimple[vertexIdx] = outVertex.Position_Tx;
}
