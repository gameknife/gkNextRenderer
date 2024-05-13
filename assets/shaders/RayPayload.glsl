
struct RayPayload
{
	vec4 ColorAndDistance; // rgb + t
	vec4 ScatterDirection; // xyz + w (is scatter needed)
	vec4 GBuffer; // normal + roughness
	uint RandomSeed;
	uint AdaptiveRay;
};
