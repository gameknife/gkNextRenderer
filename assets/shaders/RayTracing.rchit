#version 460
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_ray_query : require

#include "Material.glsl"
#include "UniformBufferObject.glsl"

layout(binding = 0, set = 0) uniform accelerationStructureEXT Scene;
layout(binding = 1) readonly buffer LightObjectArray { LightObject[] Lights; };
layout(binding = 3) readonly uniform UniformBufferObjectStruct { UniformBufferObject Camera; };
layout(binding = 4) readonly buffer VertexArray { float Vertices[]; };
layout(binding = 5) readonly buffer IndexArray { uint Indices[]; };
layout(binding = 6) readonly buffer MaterialArray { Material[] Materials; };
layout(binding = 7) readonly buffer OffsetArray { uvec2[] Offsets; };
layout(binding = 8) uniform sampler2D[] TextureSamplers;

#include "common/Const_Func.glsl"
#include "Scatter.glsl"
#include "Vertex.glsl"

hitAttributeEXT vec2 HitAttributes;
rayPayloadInEXT RayPayload Ray;

void main()
{
	// Get the material.
	const uvec2 offsets = Offsets[gl_InstanceCustomIndexEXT];
	const uint indexOffset = offsets.x + gl_PrimitiveID * 3;
	const uint vertexOffset = offsets.y;
	const Vertex v0 = UnpackVertex(vertexOffset + Indices[indexOffset]);
	const Vertex v1 = UnpackVertex(vertexOffset + Indices[indexOffset + 1]);
	const Vertex v2 = UnpackVertex(vertexOffset + Indices[indexOffset + 2]);
	const Material material = Materials[v0.MaterialIndex];

	// Compute the ray hit point properties.
	const vec3 barycentrics = vec3(1.0 - HitAttributes.x - HitAttributes.y, HitAttributes.x, HitAttributes.y);
	const vec3 normal = normalize((to_world(barycentrics, v0.Normal, v1.Normal, v2.Normal) * gl_WorldToObjectEXT).xyz);
	const vec2 texCoord = Mix(v0.TexCoord, v1.TexCoord, v2.TexCoord, barycentrics);
	
    int lightIdx = int(floor(RandomFloat(Ray.RandomSeed) * .99999 * Lights.length()));
	Ray.primitiveId = (gl_InstanceID + 1) << 16 | v0.MaterialIndex;
	Ray.BounceCount++;
	Ray.Exit = 0;
	Scatter(Ray, material, Lights[lightIdx], gl_WorldRayDirectionEXT, normal, texCoord, gl_HitTEXT, v0.MaterialIndex);
}
