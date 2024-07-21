#version 460
#extension GL_ARB_separate_shader_objects : enable
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_GOOGLE_include_directive : require
#include "common/Material.glsl"
#include "common/UniformBufferObject.glsl"
#include "common/ColorFunc.glsl"

layout(binding = 0) readonly uniform UniformBufferObjectStruct { UniformBufferObject Camera; };
layout(binding = 1) readonly buffer MaterialArray { Material[] Materials; };
//layout(binding = 2) uniform sampler2D[] TextureSamplers;
layout(set = 1, binding = 0) uniform sampler2D TextureSamplers[];

layout(location = 0) in vec3 FragColor;
layout(location = 1) in vec3 FragNormal;
layout(location = 2) in vec2 FragTexCoord;
layout(location = 3) in flat int FragMaterialIndex;

layout(location = 0) out vec4 OutColor;

void main()
{
	const vec3 normal = normalize(FragNormal);
	const int textureId = Materials[FragMaterialIndex].DiffuseTextureId;
	
	const float t = 0.5*(normal.y + 1);
	vec3 skyColor = mix(vec3(1.0), vec3(0.5, 0.7, 1.0) * 20, t);

	const vec3 lightVector = normalize(vec3(5, 4, 3));
	const float d = max(dot(lightVector, normal) * 20.0, 0.5);
	
	vec3 albedo = FragColor;
	if (textureId >= 0)
	{
		vec3 albedoraw = texture(TextureSamplers[textureId], FragTexCoord).rgb;
		albedo *= albedoraw * albedoraw;
	}

	vec3 c = albedo * d + albedo * skyColor;
	OutColor = vec4(1);
	OutColor.rgb = LinearToST2084UE(c.rgb * Camera.PaperWhiteNit / 230.0);

	
}