#version 460
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_ray_query : require

#include "common/Material.glsl"
#include "common/UniformBufferObject.glsl"

layout(binding = 0, set = 0) uniform accelerationStructureEXT Scene;
layout(binding = 1) readonly buffer LightObjectArray { LightObject[] Lights; };
layout(binding = 3) readonly uniform UniformBufferObjectStruct { UniformBufferObject Camera; };
layout(binding = 4) readonly buffer VertexArray { float Vertices[]; };
layout(binding = 5) readonly buffer IndexArray { uint Indices[]; };
layout(binding = 6) readonly buffer MaterialArray { Material[] Materials; };
layout(binding = 7) readonly buffer OffsetArray { uvec2[] Offsets; };
layout(binding = 8) uniform sampler2D[] TextureSamplers;

#include "common/RayPayload.glsl"

hitAttributeEXT vec2 HitAttributes;
rayPayloadInEXT RayPayload Ray;

#include "common/RTCommon.glsl"

void main()
{
	const int InstCustIndex = gl_InstanceCustomIndexEXT;
	const vec3 RayOrigin = gl_WorldRayOriginEXT;
	const vec3 RayDirection = gl_WorldRayDirectionEXT;
	const float RayDist = gl_HitTEXT;
	const mat4x3 WorldToObject = gl_WorldToObjectEXT;
	const vec2 TwoBaryCoords = HitAttributes;
	const vec3 HitPos = RayOrigin + RayDirection * RayDist;
	const int PrimitiveIndex = gl_PrimitiveID;
    const int InstanceID = gl_InstanceID;
	
    ProcessHit(InstCustIndex, RayDirection, RayDist, WorldToObject, TwoBaryCoords, HitPos, PrimitiveIndex, InstanceID);
}
