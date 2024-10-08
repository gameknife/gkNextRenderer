#ifndef raypayload_inc
#define raypayload_inc

struct RayPayload
{
	vec3 Attenuation;
	float Distance;
	vec4 EmitColor;
	vec3 ScatterDirection;
	bool FrontFace;
	vec4 GBuffer; // normal + roughness
	vec4 Albedo;
	uvec4 RandomSeed;
	uint AdaptiveRay;
	uint BounceCount;
	bool HitRefract;
	float pdf;
	uint primitiveId;
	bool Exit;
	uint MaterialIndex;
	vec3 HitPos;
	uint AdaptiveSample;
};

// a simple box light may enough now
struct HittableLight
{
	vec3 WorldPosMin;
	vec3 WorldPosMax;
	vec3 WorldDirection;
	float area;
};

#endif