#version 460
#extension GL_ARB_separate_shader_objects : enable
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_GOOGLE_include_directive : require
#include "Material.glsl"

layout(binding = 1) readonly buffer MaterialArray { Material[] Materials; };
layout(binding = 2) uniform sampler2D[] TextureSamplers;

layout(location = 0) in vec3 FragNormal;
layout(location = 1) in flat int FragMaterialIndex;

layout(location = 0) out vec4 OutColor;

void main() 
{
	// mini-gbuffer output
	OutColor = vec4(normalize(FragNormal) * 0.5 + 0.5, FragMaterialIndex / 255.0);
	//OutColor = vec4(FragMaterialIndex / 255.f * 20.f);
}