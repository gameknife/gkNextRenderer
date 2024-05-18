#version 460
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require
#include "UniformBufferObject.glsl"

layout(binding = 0) readonly uniform UniformBufferObjectStruct { UniformBufferObject Camera; };

layout(location = 0) in vec3 InPosition;

layout(location = 0) out flat uint g_out_primitive_index;

out gl_PerVertex
{
	vec4 gl_Position;
};

void main() 
{
    gl_Position = Camera.Projection * Camera.ModelView * vec4(InPosition, 1.0);
	g_out_primitive_index = gl_VertexIndex / 3;
}
