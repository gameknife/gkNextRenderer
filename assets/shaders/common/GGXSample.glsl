#include "Const_Func.glsl" //pi consts

#ifndef GGXSample

#define sample_vndf
#ifdef sample_vndf

// Kenta Eto and Yusuke Tokuyoshi. 2023. Bounded VNDF Sampling for Smith-GGX Reflections.
// In SIGGRAPH Asia 2023 Technical Communications (SA Technical Communications '23), December 12-15, 2023, Sydney, NSW, Australia. ACM, New York, NY, USA, 4 pages.
// https://doi.org/10.1145/3610543.3626163
vec3 ggx_sample_vndf(vec2 alpha, vec3 wi_, vec2 uv) {
vec3 wi = normalize(vec3(wi_.xy * alpha, wi_.z));
// Sample a spherical cap
//float k = (1.0f - a2) * s2 / (s2 + a2 * wi_.z * wi_.z); // Eq. 5

float b = wi.z;
if(wi_.z > 0.f) {
float a = saturate(min(alpha.x, alpha.y)); // Eq. 6
float awiz_s = a * wi_.z / (1.0f + length(wi_.xy));
b *= ((1.0f - a * a) / (1.0f + awiz_s * awiz_s));
}

float z = fma(1.0f - uv.y, 1.0f + b, -b);
float phi  = M_TWO_PI * uv.x;
vec3 o_std = vec3(sqrt(saturate(1.0f - z * z)) * vec2(cos(phi), sin(phi)), z);
// Compute the microfacet normal m
vec3 m_std = wi + o_std;
return normalize(vec3(m_std.xy * alpha, m_std.z));
}

vec3 ggxSampling(inout uvec4 RandomSeed, float roughness, vec3 normal)
{
vec3 tangent, bitangent;
ONB(normal, tangent, bitangent);
vec3 wm_ = ggx_sample_vndf(
	vec2(roughness * roughness),
	to_local(normal, tangent, bitangent, normal),
	RandomFloat2(RandomSeed)
);
return to_world(wm_, tangent, bitangent, normal);
}
#else
vec3 ggxSampling(inout uvec4 RandomSeed, float roughness, vec3 normal)
{
return AlignWithNormal( RandomInCone(RandomSeed, cos(roughness * M_PI_4)), normal );
}
#endif

#define GGXSample
#endif