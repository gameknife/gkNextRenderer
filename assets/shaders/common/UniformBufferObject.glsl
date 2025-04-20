#ifndef UniformBufferObject_glsl

#ifdef __cplusplus
#define glbool uint32_t
#define ALIGN_16 alignas(16)
#else
#define glbool bool
#define ALIGN_16
#endif

const int SHADOWMAP_SIZE = 4096;
const int CUBE_SIZE_XY = 256;
const int CUBE_SIZE_Z = 48;
const float CUBE_UNIT = 0.25f;
const vec3 CUBE_OFFSET = vec3(-CUBE_SIZE_XY / 2, -1.375f, -CUBE_SIZE_XY / 2) * CUBE_UNIT;

const float CUBE_UNIT_FAR = 4.0f; // cover 0.8km x 0.8km x 0.16km
const vec3 CUBE_OFFSET_FAR = vec3(-CUBE_SIZE_XY / 2, -1.375f, -CUBE_SIZE_XY / 2) * CUBE_UNIT_FAR;

struct ALIGN_16 UniformBufferObject
{
	mat4 ModelView;
	mat4 Projection;
	mat4 ModelViewInverse;
	mat4 ProjectionInverse;
	mat4 ViewProjection;
	mat4 PrevViewProjection;
	
	vec4 ViewportRect;
	vec4 SunDirection;
	vec4 SunColor;
	vec4 BackGroundColor;	//not used
	
	mat4 SunViewProjection;
	
	float Aperture;
	float FocusDistance;
	float SkyRotation;
	float HeatmapScale;
	
	float PaperWhiteNit;
	float SkyIntensity;
	uint SkyIdx;
	uint TotalFrames;
	
	uint MaxNumberOfBounces;
	uint NumberOfSamples;
	uint NumberOfBounces;
	uint RandomSeed;
	
	uint LightCount;
	glbool HasSky;
	glbool ShowHeatmap;
	glbool UseCheckerBoard;	//not used
	
	uint TemporalFrames;
	glbool HasSun;
	glbool HDR;
	glbool AdaptiveSample;
	
	float AdaptiveVariance;
	uint AdaptiveSteps;
	glbool TAA;
	uint SelectedId;
	
	glbool ShowEdge;
	glbool ProgressiveRender;
	float BFSigma;
	float BFSigmaLum;
	
	float BFSigmaNormal;
	uint BFSize;

	glbool BakeWithGPU;
	glbool FastGather;

	glbool FastInterpole;
	glbool DebugDraw_Lighting;
	glbool Reserve3;
	glbool Reserve4;
};

struct ALIGN_16 NodeProxy
{
uint instanceId;
uint modelId;
uint reserved1;
uint reserved2;
mat4 worldTS;
mat4 combinedPrevTS;
uint matId[16];
};

// size = 24 x 4 bytes, 96 bytes, maximun compress to 64 bytes
// if use BC1 to compress color, can reduce to 4 bytes per cube, 1 / 8 size
// and we can use the active / inactive to adjust lerp value between probe
struct ALIGN_16 AmbientCube
{
	uint PosZ;
	uint NegZ;
	uint PosY;
	uint NegY;
	uint PosX;
	uint NegX;

	uint PosZ_D;
	uint NegZ_D;
	uint PosY_D;
	uint NegY_D;
	uint PosX_D;
	uint NegX_D;

	uint PosZ_S;
	uint NegZ_S;
	uint PosY_S;
	uint NegY_S;
	uint PosX_S;
	uint NegX_S;

	uint Active;
	uint Lighting;
	uint ExtInfo1;
	uint ExtInfo2;
	uint ExtInfo3;
	uint ExtInfo4;
};

struct ALIGN_16 SphericalHarmonics
{
	// 3 bands (9 coefficients per color channel)
	float coefficients[3][9];
	float padding;
};

struct ALIGN_16 LightObject
{
	vec4 p0;
	vec4 p1;
	vec4 p3;
	vec4 normal_area;
	
	uint lightMatIdx;
	uint reserved1;
	uint reserved2;
	uint reserved3;
};


#define UniformBufferObject_glsl
#endif