

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

struct GPUVertex
{
	vec3 Position;
	uint TexcoordXY;
	uint NormalXY;
	uint NormalZTangentX;
	uint TangentYZ;
	uint TangentWMatIdx;
};


Vertex UnpackVertex(uint index)
{
	const uint vertexSize = 8;
	const uint offset = index * vertexSize;

	vec3 Position = vec3(Vertices[offset + 0], Vertices[offset + 1], Vertices[offset + 2]);
	uint TexcoordXY = floatBitsToUint(Vertices[offset + 3]);
	uint NormalXY = floatBitsToUint(Vertices[offset + 4]);
	uint NormalZTangentX = floatBitsToUint(Vertices[offset + 5]);
	uint TangentYZ = floatBitsToUint(Vertices[offset + 6]);
	uint TangentWMatIdx = floatBitsToUint(Vertices[offset + 7]);

	uint TangentW = (TangentWMatIdx >> 16) & 0xFFFF;
	uint matIdx = TangentWMatIdx & 0xFFFF;
	
	Vertex v;
	
	v.Position = Position;
	v.Normal = vec3(
		unpackHalf2x16(NormalXY).x,
		unpackHalf2x16(NormalXY).y,
		unpackHalf2x16(NormalZTangentX).x
	);
	v.Tangent = vec4(
		unpackHalf2x16(NormalZTangentX).y,
		unpackHalf2x16(TangentYZ).x,
		unpackHalf2x16(TangentYZ).y,
		float(TangentW) - 1.0f
	);
	v.TexCoord = vec2(
		unpackHalf2x16(TexcoordXY).x,
		unpackHalf2x16(TexcoordXY).y
	);
	v.MaterialIndex = matIdx;

	return v;
}

#endif