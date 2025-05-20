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
    public uint PosZ;
    public uint NegZ;
    public uint PosY;
    public uint NegY;
    public uint PosX;
    public uint NegX;
 
    public uint PosZ_D;
    public uint NegZ_D;
    public uint PosY_D;
    public uint NegY_D;
    public uint PosX_D;
    public uint NegX_D;
 
    public uint PosZ_S;
    public uint NegZ_S;
    public uint PosY_S;
    public uint NegY_S;
    public uint PosX_S;
    public uint NegX_S;
 
    public uint Active;
    public uint Lighting;
    public uint ExtInfo1;
    public uint ExtInfo2;
    public uint ExtInfo3;
    public uint ExtInfo4;
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
public const static uint MaterialLambertian = 0;
public const static uint MaterialMetallic = 1;
public const static uint MaterialDielectric = 2;
public const static uint MaterialIsotropic = 3;
public const static uint MaterialDiffuseLight = 4;
public const static uint MaterialMixture = 5;

public struct Material
{
    public float4 Diffuse;
    public int DiffuseTextureId;
    public int MRATextureId;
    public int NormalTextureId;
    public float Fuzziness;
    public float RefractionIndex;
    public uint MaterialModel;
    public float Metalness;

    public float RefractionIndex2;
    public float NormalTextureScale;
    public float Reserverd2;
};

public struct RayCastContext
{
    // in
    public float4 Origin;
    public float4 Direction;
    public float TMin;
    public float TMax;
    public float Reversed0;
    public float Reversed1;
    // out
    public float4 HitPoint;
    public float4 Normal;
    public float T;
    public uint InstanceId;
    public uint MaterialId;
    public uint Hitted;
};

public struct Vertex
{
  public float3 Position;
  public float3 Normal;
  public float4 Tangent;
  public float2 TexCoord;
  public uint MaterialIndex;
};

public struct GPUVertex
{
	public float3 Position;
	public uint TexcoordXY;
	public uint NormalXY;
	public uint NormalZTangentX;
	public uint TangentYZ;
	public uint TangentWMatIdx;
};

