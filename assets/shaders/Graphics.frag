#version 460
#extension GL_ARB_separate_shader_objects : enable
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_GOOGLE_include_directive : require

layout(location = 0) out vec4 OutColor;

void main()
{
	OutColor = vec4(0);
	OutColor.a = 0.25;
}