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
	float Metalness;
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
	mat4 prevTrans;
};

#endif