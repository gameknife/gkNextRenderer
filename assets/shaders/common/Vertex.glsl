

#ifndef vertex_inc

#define vertex_inc

struct Vertex
{
  vec3 Position;
  vec3 Normal;
  vec4 Tangent;
  vec2 TexCoord;
  uint MaterialIndex;
};

Vertex UnpackVertex(uint index)
{
	const uint vertexSize = 13;
	const uint offset = index * vertexSize;
	
	Vertex v;
	
	v.Position = vec3(Vertices[offset + 0], Vertices[offset + 1], Vertices[offset + 2]);
	v.Normal = vec3(Vertices[offset + 3], Vertices[offset + 4], Vertices[offset + 5]);
	v.Tangent = vec4(Vertices[offset + 6], Vertices[offset + 7], Vertices[offset + 8], Vertices[offset + 9]);
	v.TexCoord = vec2(Vertices[offset + 10], Vertices[offset + 11]);
	v.MaterialIndex = floatBitsToUint(Vertices[offset + 12]);

	return v;
}

#endif