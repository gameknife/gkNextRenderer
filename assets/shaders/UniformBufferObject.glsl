
struct UniformBufferObject
{
	mat4 ModelView;
	mat4 Projection;
	mat4 ModelViewInverse;
	mat4 ProjectionInverse;
	float Aperture;
	float FocusDistance;
	float HeatmapScale;
	float ColorPhi;
	float DepthPhi;
	float NormalPhi;
	uint TotalNumberOfSamples;
	uint NumberOfSamples;
	uint NumberOfBounces;
	uint RandomSeed;
	bool HasSky;
	bool ShowHeatmap;
	bool UseCheckerBoard;
	uint TemporalFrames;
};
