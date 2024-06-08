#version 460
#extension GL_ARB_separate_shader_objects : enable
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_GOOGLE_include_directive : require
#include "Material.glsl"
#include "UniformBufferObject.glsl"

layout(binding = 0) readonly uniform UniformBufferObjectStruct { UniformBufferObject Camera; };
layout(binding = 1) readonly buffer MaterialArray { Material[] Materials; };
layout(binding = 2) uniform sampler2D[] TextureSamplers;

layout(location = 0) in vec3 FragColor;
layout(location = 1) in vec3 FragNormal;
layout(location = 2) in vec2 FragTexCoord;
layout(location = 3) in flat int FragMaterialIndex;

layout(location = 0) out vec4 OutColor;

vec3 LinearToST2084UE(vec3 lin)
{
	const float m1 = 0.1593017578125; // = 2610. / 4096. * .25;
	const float m2 = 78.84375; // = 2523. / 4096. *  128;
	const float c1 = 0.8359375; // = 2392. / 4096. * 32 - 2413./4096.*32 + 1;
	const float c2 = 18.8515625; // = 2413. / 4096. * 32;
	const float c3 = 18.6875; // = 2392. / 4096. * 32;
	const float C = 10000.;

	vec3 L = lin/C;
	vec3 Lm = pow(L, vec3(m1));
	vec3 N1 = ( c1 + c2 * Lm );
	vec3 N2 = ( 1.0 + c3 * Lm );
	vec3 N = N1 * (1.0 / N2);
	vec3 P = pow( N, vec3(m2) );

	return P;
}

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