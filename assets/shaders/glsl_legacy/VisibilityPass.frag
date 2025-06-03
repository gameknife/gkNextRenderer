#version 460
#extension GL_ARB_separate_shader_objects : enable
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_GOOGLE_include_directive : require
#include "common/Material.glsl"

layout (location = 0) flat in uint g_primitive_index;
layout (location = 1) flat in uint g_instance_index;
layout(location = 0) out uvec2 g_out_visibility;

void main() 
{
	g_out_visibility.y = g_primitive_index;
	g_out_visibility.x = g_instance_index + 1;
}