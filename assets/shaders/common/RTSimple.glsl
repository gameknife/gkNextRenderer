#include "Vertex.glsl"

void SimpleHit(const int InstCustIndex, const mat4x3 WorldToObject, const vec2 TwoBaryCoords, const int PrimitiveIndex, const int InstanceID, out vec3 HitNormal, out vec2 HitTexcoord, out uint MaterialId, out uint OutInstanceId  )
{
    // Get the material.
    const NodeProxy node = NodeProxies[InstanceID];
    const uvec2 offsets = Offsets[node.modelId];
    const uint indexOffset = offsets.x + PrimitiveIndex * 3;
    const uint vertexOffset = offsets.y;
    const Vertex v0 = UnpackVertex(vertexOffset + Indices[indexOffset]);
    const Vertex v1 = UnpackVertex(vertexOffset + Indices[indexOffset + 1]);
    const Vertex v2 = UnpackVertex(vertexOffset + Indices[indexOffset + 2]);
    
    MaterialId = node.matId[v0.MaterialIndex];

    // Compute the ray hit point properties.
    const vec3 barycentrics = vec3(1.0 - TwoBaryCoords.x - TwoBaryCoords.y, TwoBaryCoords.x, TwoBaryCoords.y);
    HitNormal = normalize((Mix(v0.Normal, v1.Normal, v2.Normal, barycentrics) * WorldToObject).xyz);
    HitTexcoord = Mix(v0.TexCoord, v1.TexCoord, v2.TexCoord, barycentrics);
    OutInstanceId = node.instanceId;
}