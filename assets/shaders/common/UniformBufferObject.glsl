#ifdef __cplusplus
#define glbool uint32_t
#else
#define glbool bool
#endif

struct UniformBufferObject
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
	vec4 BackGroundColor;
	
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
	glbool UseCheckerBoard;
	uint TemporalFrames;
	glbool HasSun;
	glbool HDR;
	glbool AdaptiveSample;
	float AdaptiveVariance;
	uint AdaptiveSteps;
	glbool TAA;
	uint SelectedId;
		
	float BFSigma;
	float BFSigmaLum;
	uint BFSize;
};
