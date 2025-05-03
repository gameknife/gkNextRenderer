implementing Common;

public struct UniformBufferObject
{
    public matrix ModelView;
    public matrix Projection;
    public matrix ModelViewInverse;
    public matrix ProjectionInverse;
    public matrix ViewProjection;
    public matrix PrevViewProjection;

    public float4 ViewportRect;
    public float4 SunDirection;
    public float4 SunColor;
    public float4 BackGroundColor;    //not used

    public matrix SunViewProjection;

    public float Aperture;
    public float FocusDistance;
    public float SkyRotation;
    public float HeatmapScale;
 
    public float PaperWhiteNit;
    public float SkyIntensity;
    public uint SkyIdx;
    public uint TotalFrames;
 
    public uint MaxNumberOfBounces;
    public uint NumberOfSamples;
    public uint NumberOfBounces;
    public uint RandomSeed;
 
    public uint LightCount;
    public bool HasSky;
    public bool ShowHeatmap;
    public bool UseCheckerBoard;    //not used
 
    public uint TemporalFrames;
    public bool HasSun;
    public bool HDR;
    public bool AdaptiveSample;
 
    public float AdaptiveVariance;
    public uint AdaptiveSteps;
    public bool TAA;
    public uint SelectedId;
 
    public bool ShowEdge;
    public bool ProgressiveRender;
    public float BFSigma;
    public float BFSigmaLum;
 
    public float BFSigmaNormal;
    public uint BFSize;
 
    public bool FastGather;
 
    public bool FastInterpole;
    public bool DebugDraw_Lighting;
    public bool Reserve3;
    public bool Reserve4;
};

public struct NodeProxy
{
    public uint instanceId;
    public uint modelId;
    public uint reserved1;
    public uint reserved2;
    public matrix worldTS;
    public matrix combinedPrevTS;
    public uint matId[16];
};

// size = 24 x 4 bytes, 96 bytes, maximun compress to 64 bytes
// if use BC1 to compress color, can reduce to 4 bytes per cube, 1 / 8 size
// and we can use the active / inactive to adjust lerp value between probe
public struct AmbientCube
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

public struct SphericalHarmonics
{
    // 3 bands (9 coefficients per color channel)
    public float coefficients[3][9];
    public float padding;
};

public struct LightObject
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
