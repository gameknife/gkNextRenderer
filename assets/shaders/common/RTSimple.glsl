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

int TracingOccludeFunction(in vec3 origin, in vec3 lightPos)
{
    float dist = length(lightPos - origin);
    vec3 lightDir = (lightPos - origin) / dist;

    rayQueryEXT rayQuery;
    rayQueryInitializeEXT(rayQuery, Scene, gl_RayFlagsTerminateOnFirstHitEXT, 0xFF, origin, EPS, lightDir, dist - EPS2);

    rayQueryProceedEXT(rayQuery);

    if (rayQueryGetIntersectionTypeEXT(rayQuery, true) == gl_RayQueryCommittedIntersectionTriangleEXT) {
        return 0;
    }
    else
    {
        return 1;
    }
}

// return if hits, this function may differ between Shader & Cpp
bool TracingFunction(in vec3 origin, in vec3 rayDir, out vec3 OutNormal, out uint OutMaterialId, out float OutRayDist)
{
    rayQueryEXT rayQuery;
    rayQueryInitializeEXT(rayQuery, Scene, gl_RayFlagsNoneEXT, 0xFF, origin.xyz, EPS, rayDir, 10.0f);

    while( rayQueryProceedEXT(rayQuery) )
    {

    }

    if (rayQueryGetIntersectionTypeEXT(rayQuery, true) == gl_RayQueryCommittedIntersectionTriangleEXT  ) {
        const bool IsCommitted = true;
        const int InstCustIndex = rayQueryGetIntersectionInstanceCustomIndexEXT(rayQuery, IsCommitted);
        const float RayDist = rayQueryGetIntersectionTEXT(rayQuery, IsCommitted);
        const mat4x3 WorldToObject = rayQueryGetIntersectionWorldToObjectEXT(rayQuery, IsCommitted);
        const vec2 TwoBaryCoords = rayQueryGetIntersectionBarycentricsEXT(rayQuery, IsCommitted);
        const int PrimitiveIndex = rayQueryGetIntersectionPrimitiveIndexEXT(rayQuery, IsCommitted);
        const int InstanceID = rayQueryGetIntersectionInstanceIdEXT(rayQuery, IsCommitted);
        OutRayDist = RayDist;

        vec2 OutTexcoord;
        uint OutInstanceId;
        SimpleHit(InstCustIndex, WorldToObject, TwoBaryCoords, PrimitiveIndex, InstanceID, OutNormal, OutTexcoord, OutMaterialId, OutInstanceId);
        return true;
    }

    return false;
}
