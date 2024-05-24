
struct RayPayload
{
	vec3 Attenuation;
	float Distance;
	vec4 EmitColor;
	vec3 ScatterDirection;
	uint FrontFace;
	vec4 GBuffer; // normal + roughness
	uint RandomSeed;
	uint AdaptiveRay;
	uint BounceCount;
	float pdf;
};
