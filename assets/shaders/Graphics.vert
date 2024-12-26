#version 460
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require
#include "common/Material.glsl"
#include "common/UniformBufferObject.glsl"

layout(binding = 0) readonly uniform UniformBufferObjectStruct { UniformBufferObject Camera; };
layout(binding = 1) readonly buffer MaterialArray { Material[] Materials; };

layout(location = 0) in vec3 InPosition;
//layout(location = 1) in vec3 InNormal;
//layout(location = 2) in vec3 InTangent;
//layout(location = 3) in vec2 InTexCoord;
//layout(location = 4) in uint InMaterialIndex;

layout(location = 0) out vec3 FragColor;
layout(location = 1) out vec3 FragNormal;
layout(location = 2) out vec2 FragTexCoord;
layout(location = 3) out flat uint FragMaterialIndex;

layout(push_constant) uniform PushConsts {
	mat4 worldMatrix;
} pushConsts;

out gl_PerVertex
{
	vec4 gl_Position;
};

void main()
{
	Material m = Materials[0];

	gl_Position = Camera.Projection * Camera.ModelView * pushConsts.worldMatrix * vec4(InPosition, 1.0);
//	FragColor = m.Diffuse.xyz;
//	FragNormal = vec3(pushConsts.worldMatrix * vec4(InNormal, 0.0)); // technically not correct, should be ModelInverseTranspose
//	FragTexCoord = InTexCoord;
//	FragMaterialIndex = InMaterialIndex;
}