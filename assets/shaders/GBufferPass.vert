#version 460
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require
#include "common/Material.glsl"
#include "common/UniformBufferObject.glsl"

#define PURE_VERTEX_DEF
#include "common/Vertex.glsl"

layout(binding = 0) readonly uniform UniformBufferObjectStruct { UniformBufferObject Camera; };
layout(binding = 1) readonly buffer MaterialArray { Material[] Materials; };
layout(binding = 2) readonly buffer NodeProxyArray { NodeProxy[] NodeProxies; };

layout(location = 0) in vec3 InPosition;
layout(location = 1) in uint TexcoordXY;
layout(location = 2) in uint NormalXY;
layout(location = 3) in uint NormalZTangentX;
layout(location = 4) in uint TangentYZ;
layout(location = 5) in uint TangentWMatIdx;

layout(location = 0) out vec3 FragColor;
layout(location = 1) out vec3 FragNormal;
layout(location = 2) out vec2 FragTexCoord;
layout(location = 3) out flat uint FragMaterialIndex;

out gl_PerVertex
{
	vec4 gl_Position;
};
		
void main()
{
	NodeProxy proxy = NodeProxies[gl_InstanceIndex];
	gl_Position = Camera.Projection * Camera.ModelView * proxy.worldTS * vec4(InPosition, 1.0);
	
	Vertex v = UncompressVertex(InPosition, TexcoordXY, NormalXY, NormalZTangentX, TangentYZ, TangentWMatIdx);
	Material m = Materials[proxy.matId[v.MaterialIndex]];
	FragColor = m.Diffuse.xyz;
	FragNormal = (proxy.worldTS * vec4(v.Normal, 0.0)).xyz; 
	FragTexCoord = v.TexCoord;
	FragMaterialIndex = proxy.matId[v.MaterialIndex];
}