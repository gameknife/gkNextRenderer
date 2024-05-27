
struct RayPayload
{
	vec3 Attenuation;
	float Distance;
	vec4 EmitColor;
	vec3 ScatterDirection;
	uint FrontFace;
	vec4 GBuffer; // normal + roughness
	vec4 Albedo;
	uint RandomSeed;
	uint AdaptiveRay;
	uint BounceCount;
	float pdf;
	uint primitiveId;
};

// a simple box light may enough now
struct HittableLight
{
	vec3 WorldPosMin;
	vec3 WorldPosMax;
	vec3 WorldDirection;
	float area;
};