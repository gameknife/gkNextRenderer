#ifndef UniformBufferObject_hlsl
#define UniformBufferObject_hlsl

#ifdef __cplusplus
#define glbool uint32_t
#define ALIGN_16 alignas(16)
#else
#define glbool bool
#define ALIGN_16
#endif

static const int SHADOWMAP_SIZE = 4096;
static const int CUBE_SIZE_XY = 192;//256;
static const int CUBE_SIZE_Z = 48;
static const float CUBE_UNIT = 0.25f;
static const float3 CUBE_OFFSET = float3(-CUBE_SIZE_XY / 2, -1.375f, -CUBE_SIZE_XY / 2) * CUBE_UNIT;

static const float CUBE_UNIT_FAR = 4.0f; // cover 0.8km x 0.8km x 0.16km
static const float3 CUBE_OFFSET_FAR = float3(-CUBE_SIZE_XY / 2, -1.375f, -CUBE_SIZE_XY / 2) * CUBE_UNIT_FAR;

struct UniformBufferObject
{
    matrix ModelView;
    matrix Projection;
    matrix ModelViewInverse;
    matrix ProjectionInverse;
    matrix ViewProjection;
    matrix PrevViewProjection;

    float4 ViewportRect;
    float4 SunDirection;
    float4 SunColor;
    float4 BackGroundColor;    //not used

    matrix SunViewProjection;

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
    glbool UseCheckerBoard;    //not used

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

    glbool FastGather;

    glbool FastInterpole;
    glbool DebugDraw_Lighting;
    glbool Reserve3;
    glbool Reserve4;
};

struct NodeProxy
{
    uint instanceId;
    uint modelId;
    uint reserved1;
    uint reserved2;
    matrix worldTS;
    matrix combinedPrevTS;
    uint matId[16];
};

// size = 24 x 4 bytes, 96 bytes, maximun compress to 64 bytes
// if use BC1 to compress color, can reduce to 4 bytes per cube, 1 / 8 size
// and we can use the active / inactive to adjust lerp value between probe
struct AmbientCube
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

struct SphericalHarmonics
{
    // 3 bands (9 coefficients per color channel)
    float coefficients[3][9];
    float padding;
};

struct LightObject
{
    float4 p0;
    float4 p1;
    float4 p3;
    float4 normal_area;

    uint lightMatIdx;
    uint reserved1;
    uint reserved2;
    uint reserved3;
};

#endif // UniformBufferObject_hlsl