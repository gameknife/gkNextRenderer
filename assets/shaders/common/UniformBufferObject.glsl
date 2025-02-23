#ifdef __cplusplus
#define glbool uint32_t
#define ALIGN_16 alignas(16)
#else
#define glbool bool
#define ALIGN_16
#endif

const int CUBE_SIZE_XY = 200;
const int CUBE_SIZE_Z = 50;
const float CUBE_UNIT = 0.25f;
const vec3 CUBE_OFFSET = vec3(-CUBE_SIZE_XY / 2, -1.5, -CUBE_SIZE_XY / 2) * CUBE_UNIT;

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

// size = 32 bytes
struct ALIGN_16 AmbientCube
{
	uint PosZ;
	uint NegZ;
	uint PosY;
	uint NegY;
	uint PosX;
	uint NegX;
	ivec2 Info;
};
