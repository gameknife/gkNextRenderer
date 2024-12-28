#version 460
#extension GL_ARB_separate_shader_objects : enable
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_GOOGLE_include_directive : require
#include "common/Material.glsl"

layout(binding = 1) readonly buffer MaterialArray { Material[] Materials; };
layout(set = 1, binding = 0) uniform sampler2D TextureSamplers[];

layout(location = 0) in vec3 FragColor;
layout(location = 1) in vec3 FragNormal;
layout(location = 2) in vec2 FragTexCoord;
layout(location = 3) in flat uint FragMaterialIndex;

layout(location = 0) out vec4 OutColor;
layout(location = 1) out vec4 OutNormal;
layout(location = 2) out vec4 OutReserved;

void main()
{
	const int textureId = Materials[FragMaterialIndex].DiffuseTextureId;

	vec3 c = FragColor;
	if (textureId >= 0)
	{
		vec3 albedo = texture(TextureSamplers[textureId], FragTexCoord).rgb;
		c *= albedo * albedo;
	}

	OutColor = vec4(c, 1);
	OutNormal = vec4(FragNormal, Materials[FragMaterialIndex].Fuzziness);
	OutReserved = vec4(0,0,0,0);
}