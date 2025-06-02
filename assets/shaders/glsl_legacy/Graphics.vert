#version 460
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require
#include "common/Material.glsl"
#include "common/UniformBufferObject.glsl"

layout(binding = 0) readonly uniform UniformBufferObjectStruct { UniformBufferObject Camera; };
layout(binding = 1) readonly buffer MaterialArray { Material[] Materials; };

layout(location = 0) in vec3 InPosition;

layout(push_constant) uniform PushConsts {
	mat4 worldMatrix;
} pushConsts;

out gl_PerVertex
{
	vec4 gl_Position;
};

void main()
{
	gl_Position = Camera.Projection * Camera.ModelView * pushConsts.worldMatrix * vec4(InPosition, 1.0);
}