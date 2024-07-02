#extension GL_EXT_ray_query : require
#extension GL_EXT_nonuniform_qualifier : require

#define USE_FIREFLY_FILTER 1
#include "Vertex.glsl"
#include "Random.glsl"
#include "common/equirectangularSample.glsl"
#include "Scatter.glsl"

void ProcessHit(const int InstCustIndex, const vec3 RayDirection, const float RayDist, const mat4x3 WorldToObject, const vec2 TwoBaryCoords, const vec3 HitPos, const int PrimitiveIndex, const int InstanceID)
{
    // Get the material.
	const uvec2 offsets = Offsets[InstCustIndex];
	const uint indexOffset = offsets.x + PrimitiveIndex * 3;
	const uint vertexOffset = offsets.y;
	const Vertex v0 = UnpackVertex(vertexOffset + Indices[indexOffset]);
	const Vertex v1 = UnpackVertex(vertexOffset + Indices[indexOffset + 1]);
	const Vertex v2 = UnpackVertex(vertexOffset + Indices[indexOffset + 2]);
	const Material material = Materials[v0.MaterialIndex];

	// Compute the ray hit point properties.
	const vec3 barycentrics = vec3(1.0 - TwoBaryCoords.x - TwoBaryCoords.y, TwoBaryCoords.x, TwoBaryCoords.y);
	const vec3 normal = normalize((to_world(barycentrics, v0.Normal, v1.Normal, v2.Normal) * WorldToObject).xyz);
	const vec2 texCoord = Mix(v0.TexCoord, v1.TexCoord, v2.TexCoord, barycentrics);
	
    int lightIdx = int(floor(RandomFloat(Ray.RandomSeed) * .99999 * Lights.length()));
    Ray.HitPos = HitPos; 
	Ray.primitiveId = (InstanceID + 1) << 16 | v0.MaterialIndex;
	Ray.BounceCount++;
	Ray.Exit = false;
	Scatter(Ray, material, Lights[lightIdx], RayDirection, normal, texCoord, RayDist, v0.MaterialIndex);
}

void ProcessMiss(const vec3 RayDirection)
{
	Ray.GBuffer = vec4(0,1,0,0);
	Ray.Albedo = vec4(1,1,1,1);
	Ray.primitiveId = 0;
	Ray.Exit = true;
	Ray.Distance = -10;
	Ray.pdf = 1.0;
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
        outRayColor *= vec3(0);
        return true;
    }
// no gain performance now, but loose quality, scene 5 check
//    else if(Ray.BounceCount > Camera.RR_MIN_DEPTH)
//    {
//    	const Material material = Materials[Ray.MaterialIndex];
//    	float rr_scale =  material.MaterialModel == MaterialDielectric ? (Ray.FrontFace ? 1.0 / material.RefractionIndex : material.RefractionIndex) : 1.0;
//    	float rr_prob = min(0.95f, luminance(outRayColor) * rr_scale);
//    	if (rr_prob < RandomFloat(Ray.RandomSeed))
//    	{
//    		return true;
//    	}
//    	outRayColor *= min( 1.f / rr_prob, 20.f );
//    }

    origin = origin + scatterDir * Ray.Distance;
    scatterDir = Ray.ScatterDirection;

    outRayColor *= Ray.Exit ? Ray.EmitColor.rgb : Ray.Attenuation * Ray.pdf;
    
#if USE_FIREFLY_FILTER
  float lum = dot(outRayColor, vec3(0.212671F, 0.715160F, 0.072169F));
  if(lum > 1600.0F)
  {
    outRayColor *= 1600.0F / lum;
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
	vec2 currfpos = vec2((currFrameHPos.xy / currFrameHPos.w * 0.5 + 0.5) * vec2(size));
	vec4 prevFrameHPos = Camera.PrevViewProjection * vec4(origin, 1);
	vec2 prevfpos = vec2((prevFrameHPos.xy / prevFrameHPos.w * 0.5 + 0.5) * vec2(size));
	motionVector = Ray.Distance < -5 ? vec4(0) : vec4(prevfpos - currfpos,0,0);
	primitiveId = Ray.primitiveId;
}