#extension GL_EXT_ray_query : require
#extension GL_EXT_nonuniform_qualifier : require

#define USE_FIREFLY_FILTER 1
#include "Vertex.glsl"
#include "Random.glsl"
#include "common/equirectangularSample.glsl"
#include "Scatter.glsl"
#include "RTSimple.glsl"

vec3 mikkTSpace(vec3 vNt, vec3 normal, vec4 tangent)
{
    vNt = vNt * 2.0 - 1.0;
    vec3 vB = tangent.w * cross(normal, tangent.xyz);
    normal = normalize( vNt.x * tangent.xyz + vNt.y * vB + vNt.z * normal );
    return normal;
}

void ProcessHit(const int InstCustIndex, const vec3 RayDirection, const float RayDist, const mat4x3 WorldToObject, const vec2 TwoBaryCoords, const vec3 HitPos, const int PrimitiveIndex, const int InstanceID)
{
    // Get the material.
    const NodeProxy node = NodeProxies[InstanceID];
	const uvec2 offsets = Offsets[node.modelId];
	const uint indexOffset = offsets.x + PrimitiveIndex * 3;
	const uint vertexOffset = offsets.y;
	const Vertex v0 = UnpackVertex(vertexOffset + Indices[indexOffset]);
	const Vertex v1 = UnpackVertex(vertexOffset + Indices[indexOffset + 1]);
	const Vertex v2 = UnpackVertex(vertexOffset + Indices[indexOffset + 2]);
	const Material material = Materials[node.matId[v0.MaterialIndex]]; // read real materialIdx from node, vertex store the index

	// Compute the ray hit point properties.
	const vec3 barycentrics = vec3(1.0 - TwoBaryCoords.x - TwoBaryCoords.y, TwoBaryCoords.x, TwoBaryCoords.y);
    const vec2 texCoord = Mix(v0.TexCoord, v1.TexCoord, v2.TexCoord, barycentrics);
    
    vec3 normal;
    if(material.NormalTextureId >= 0)
    {
        // normal mapping use interpolated normal
        vec3 vNt = texture(TextureSamplers[nonuniformEXT(material.NormalTextureId)], texCoord).xyz;
        vec3 localnormal = Mix(v0.Normal, v1.Normal, v2.Normal, barycentrics);
        vec4 localtangent = Mix(v0.Tangent, v1.Tangent, v2.Tangent, barycentrics);
        normal = mikkTSpace(vNt, localnormal, localtangent);
        normal = normalize((normal * WorldToObject).xyz);
    }
    else
    {
        normal = normalize((Mix(v0.Normal, v1.Normal, v2.Normal, barycentrics) * WorldToObject).xyz);
    }

    int lightIdx = int(floor(RandomFloat(Ray.RandomSeed) * .99999 * Camera.LightCount));
    Ray.HitPos = HitPos; 
	//Ray.primitiveId = InstCustIndex; // node.instanceId;
    Ray.primitiveId = node.instanceId;
	Ray.BounceCount++;
	Ray.Exit = false;
    Ray.prevTrans = node.combinedPrevTS;
	Scatter(Ray, material, Lights[lightIdx], RayDirection, normal, texCoord, RayDist, v0.MaterialIndex);
}

void ProcessMiss(const vec3 RayDirection)
{
	Ray.GBuffer = vec4(0,1,0,0);
	Ray.Albedo = vec4(1,1,1,1);
	Ray.primitiveId = 65535;
	Ray.Exit = true;
	Ray.Distance = 1000.0;
	Ray.pdf = 1.0;
    Ray.prevTrans = mat4(1);
	if (Camera.HasSky)
	{
		// Sky color
		const vec3 skyColor = equirectangularSample(RayDirection, Camera.SkyRotation).rgb * Camera.SkyIntensity;
        Ray.Attenuation = vec3(1);
		Ray.EmitColor = vec4(skyColor, -1);
	}
	else
	{
		Ray.Attenuation = vec3(0);
		Ray.EmitColor = vec4(0);
	}
}

#ifdef RT_PIPELINE
void traceRay(in vec3 origin, in vec3 scatterDir)
{
    traceRayEXT(Scene, gl_RayFlagsOpaqueEXT, 0xff, 0, 0, 0,origin.xyz, EPS, scatterDir, INF, 0);
}
#else
// trace ray, write info into RayPayload
void traceRay(in vec3 origin, in vec3 scatterDir)
{
    rayQueryEXT rayQuery;
    // gl_RayFlagsTerminateOnFirstHitEXT for fast, but hit not the closet
    rayQueryInitializeEXT(rayQuery, Scene, gl_RayFlagsNoneEXT, 0xFF, origin.xyz, EPS, scatterDir.xyz, INF);

    while( rayQueryProceedEXT(rayQuery) )
    {

    }

    if (rayQueryGetIntersectionTypeEXT(rayQuery, true) == gl_RayQueryCommittedIntersectionTriangleEXT  ) {
        const bool IsCommitted = true;
        const int InstCustIndex = rayQueryGetIntersectionInstanceCustomIndexEXT(rayQuery, IsCommitted);
        const vec3 RayOrigin = origin;
        const vec3 RayDirection = scatterDir;
        const float RayDist = rayQueryGetIntersectionTEXT(rayQuery, IsCommitted);
        const mat4x3 WorldToObject = rayQueryGetIntersectionWorldToObjectEXT(rayQuery, IsCommitted);
        const vec2 TwoBaryCoords = rayQueryGetIntersectionBarycentricsEXT(rayQuery, IsCommitted);
        const vec3 HitPos = RayOrigin + RayDirection * RayDist;
        const int PrimitiveIndex = rayQueryGetIntersectionPrimitiveIndexEXT(rayQuery, IsCommitted);
        const int InstanceID = rayQueryGetIntersectionInstanceIdEXT(rayQuery, IsCommitted);
        ProcessHit(InstCustIndex, RayDirection, RayDist, WorldToObject, TwoBaryCoords, HitPos, PrimitiveIndex, InstanceID);
    }
    else
    {
        ProcessMiss(scatterDir);
    }
}
#endif

bool GetRayColor(inout vec3 origin, inout vec3 scatterDir, inout vec3 outRayColor)
{
    traceRay(origin, scatterDir);
    // out of limit, invalid sample, return
    if(Ray.BounceCount == Camera.NumberOfBounces)
    {
        outRayColor = vec3(0);
        return true;
    }
    
    origin += scatterDir * Ray.Distance;
    if(!Ray.HitRefract) origin -= scatterDir * EPS2;
    scatterDir = Ray.ScatterDirection;

    outRayColor *= Ray.Exit ? Ray.EmitColor.rgb : Ray.Attenuation * Ray.pdf;
    
#if USE_FIREFLY_FILTER
  float lum = luminance(outRayColor);
  if(lum > 1000.0F)
  {
    outRayColor *= 1000.0F / lum;
  }
#endif

    return Ray.Exit;
}

		
void FetchPrimaryRayInfo(in vec2 size, in vec3 origin, in vec3 scatterDir, out vec4 gbuffer, out vec4 albedo, out vec4 motionVector, out uint primitiveId)
{
	// fetch albedo
	gbuffer = vec4(Ray.GBuffer.xyz, Ray.Distance);
	albedo = vec4(Ray.Albedo.rgb, Ray.GBuffer.w);
	
	vec4 currFrameHPos = Camera.ViewProjection * vec4(origin, 1);
	vec4 prevFrameHPos = Camera.PrevViewProjection * Ray.prevTrans * vec4(origin, 1);
	motionVector = vec4((prevFrameHPos.xy / prevFrameHPos.w - currFrameHPos.xy / currFrameHPos.w) * 0.5 * size, 0, 0);
	primitiveId = Ray.primitiveId;
}