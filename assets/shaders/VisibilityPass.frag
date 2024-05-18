#version 460
#extension GL_ARB_separate_shader_objects : enable
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_GOOGLE_include_directive : require
#include "Material.glsl"

layout (location = 0) flat in uint g_primitive_index;
layout(location = 0) out uint g_out_color;

void main() 
{
	g_out_color = g_primitive_index;
}