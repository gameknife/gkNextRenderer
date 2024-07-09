#include "Vertex.glsl"

void SimpleHit(const int InstCustIndex, const mat4x3 WorldToObject, const vec2 TwoBaryCoords, const int PrimitiveIndex, out vec3 HitNormal, out vec2 HitTexcoord, out int MaterialId )
{
    // Get the material.
    const uvec2 offsets = Offsets[InstCustIndex];
    const uint indexOffset = offsets.x + PrimitiveIndex * 3;
    const uint vertexOffset = offsets.y;
    const Vertex v0 = UnpackVertex(vertexOffset + Indices[indexOffset]);
    const Vertex v1 = UnpackVertex(vertexOffset + Indices[indexOffset + 1]);
    const Vertex v2 = UnpackVertex(vertexOffset + Indices[indexOffset + 2]);
    
    MaterialId = v0.MaterialIndex;

    // Compute the ray hit point properties.
    const vec3 barycentrics = vec3(1.0 - TwoBaryCoords.x - TwoBaryCoords.y, TwoBaryCoords.x, TwoBaryCoords.y);
    HitNormal = normalize((to_world(barycentrics, v0.Normal, v1.Normal, v2.Normal) * WorldToObject).xyz);
    HitTexcoord = Mix(v0.TexCoord, v1.TexCoord, v2.TexCoord, barycentrics);
}