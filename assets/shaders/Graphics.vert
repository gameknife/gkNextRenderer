#version 460
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require
#include "Material.glsl"
#include "UniformBufferObject.glsl"

layout(binding = 0) readonly uniform UniformBufferObjectStruct { UniformBufferObject Camera; };
layout(binding = 1) readonly buffer MaterialArray { Material[] Materials; };

layout(location = 0) in vec3 InPosition;
layout(location = 1) in vec3 InNormal;
layout(location = 2) in vec2 InTexCoord;
layout(location = 3) in int InMaterialIndex;

// just like visibility buffer, save the material index, and a normal, totally 32bit mini-gbuffer for bandwidth saving, after impl, switch to full visibility buffer
layout(location = 0) out vec3 FragNormal;
layout(location = 1) out flat int FragMaterialIndex;

out gl_PerVertex
{
	vec4 gl_Position;
};

void main() 
{
    gl_Position = Camera.Projection * Camera.ModelView * vec4(InPosition, 1.0);
	FragNormal = InNormal;
	FragMaterialIndex = InMaterialIndex;
}
