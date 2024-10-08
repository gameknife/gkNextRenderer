#include "common/Const_Func.glsl"

#ifndef random_inc
#define random_inc

#extension GL_EXT_control_flow_attributes : require

void pcg4d(inout uvec4 v)
{
    v = v * 1664525u + 1013904223u;
    v.x += v.y * v.w; v.y += v.z * v.x; v.z += v.x * v.y; v.w += v.y * v.z;
    v = v ^ (v >> 16u);
    v.x += v.y * v.w; v.y += v.z * v.x; v.z += v.x * v.y; v.w += v.y * v.z;
}

// Returns a float between 0 and 1
#define uint_to_float(x) ( uintBitsToFloat(0x3f800000 | ((x) >> 9)) - 1.0f )

uvec4 InitRandomSeed(uint val0, uint val1, uint frame_num)
{
	return uvec4(val0, val1, frame_num, 0);
}

float RandomFloat(inout uvec4 v)
{
	pcg4d(v);
	return uint_to_float(v.x);
}

vec2 RandomFloat2(inout uvec4 v)
{
	pcg4d(v);
	return uint_to_float(v.xy);
}

vec2 concentric_sample_disk(vec2 offset) {
    offset += offset - vec2(1);
    if (isZERO(offset.x) && isZERO(offset.y)) {
		return vec2(0);
	}

	float theta;

	if (abs(offset.x) > abs(offset.y)) {
        theta = M_PI_4 * offset.y / offset.x;
        return offset.x * vec2(cos(theta), sin(theta));
	}

	float cos_theta = sin(M_PI_4 * offset.x / offset.y);
	return offset.y * vec2(cos_theta, sqrt(1. - cos_theta * cos_theta));
}

vec2 RandomInUnitDisk(inout uvec4 seed)
{
	return concentric_sample_disk(RandomFloat2(seed));
}

vec3 RandomInCone(inout uvec4 seed, float cos_theta) {
    const vec2 u = RandomFloat2(seed);
    float phi = M_TWO_PI * u.x;

    cos_theta = 1.0f + u.y * (cos_theta - 1.f);

    return vec3(sqrt(1.0f - cos_theta * cos_theta) * vec2(cos(phi), sin(phi)), cos_theta);
}

vec3 RandomInHemiSphere(inout uvec4 seed)
{
    const vec2 u = RandomFloat2(seed);
    float phi = M_TWO_PI * u.x;

    return vec3(sqrt(1.0f - u.y) * vec2(cos(phi), sin(phi)), sqrt(u.y));
}

vec3 RandomInHemiSphere1(inout uvec4 seed)
{
    const vec2 u = RandomFloat2(seed);
    float phi = M_TWO_PI * u.x;

    return vec3(sqrt(u.y) * vec2(cos(phi), sin(phi)), sqrt(1.0-u.y));
}
#endif