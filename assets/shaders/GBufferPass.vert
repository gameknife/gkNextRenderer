#version 460
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require
#include "Material.glsl"
#include "UniformBufferObject.glsl"

layout(binding = 0) readonly uniform UniformBufferObjectStruct { UniformBufferObject Camera; };
layout(binding = 1) readonly buffer MaterialArray { Material[] Materials; };
layout(binding = 2) readonly buffer NodeProxyArray { NodeProxy[] NodeProxies; };

layout(location = 0) in vec3 InPosition;
layout(location = 1) in vec3 InNormal;
layout(location = 2) in vec2 InTexCoord;
layout(location = 3) in int InMaterialIndex;

layout(location = 0) out vec3 FragColor;
layout(location = 1) out vec3 FragNormal;
layout(location = 2) out vec2 FragTexCoord;
layout(location = 3) out flat int FragMaterialIndex;

out gl_PerVertex
{
	vec4 gl_Position;
};

void main()
{
	Material m = Materials[InMaterialIndex];

	NodeProxy proxy = NodeProxies[gl_InstanceIndex];

	vec4 worldPosition = proxy.World * vec4(InPosition, 1.0);

	gl_Position = Camera.Projection * Camera.ModelView * worldPosition;
	FragColor = m.Diffuse.xyz;
	FragNormal = InNormal; 
	FragTexCoord = InTexCoord;
	FragMaterialIndex = InMaterialIndex;
}