#ifdef __cplusplus
#define ALIGN_16 alignas(16) 
#define ALIGN_8 alignas(8)
#else
#define ALIGN_16
#define ALIGN_8
implementing Common;
#endif

public static const int MAX_NODES = 65535; // 256;
public static const int MAX_MATERIALS = 8192;

public struct ALIGN_16 GPUDrivenStat
{
    public uint ProcessedCount;
    public uint VisibleCount;
    public uint CulledCount;
    public uint TriangleCount;
    public uint CulledTriangleCount;
};

public struct ALIGN_16 UniformBufferObject
{
    public float4x4 ModelView;
    public float4x4 Projection;
    public float4x4 ModelViewInverse;
    public float4x4 ProjectionInverse;
    public float4x4 ViewProjection;
    public float4x4 PrevViewProjection;
    public float4x4 ViewProjectionUnJit;
    public float4x4 PrevViewProjectionUnJit;

    public float4 ViewportRect;
    public float4 SunDirection;
    public float4 SunColor;
    public float4 BackGroundColor;    //not used

    public float4x4 SunViewProjection;

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
    public bool DisableSpatialReuse;
    public uint SuperResolution;
};

public struct ALIGN_16 NodeProxy
{
    public uint instanceId;
    public uint modelId;
    public uint visible;
    public uint nort;
    public float4x4 worldTS;
    public float4x4 combinedPrevTS;
    public uint matId[16];
};

public struct ALIGN_16 ModelData
{
    public uint indexOffset;
    public uint indexCount;
    public uint vertexOffset;
    public uint vertexCount;

    public float4 localAabbMin; // local bounding box min
    public float4 localAabbMax; // local bounding box max

    public uint modelType;
    public uint voxelDataIdx;
    public uint reorderOffset;
    public uint reserved2;
};

// size = 14 x 4 bytes, 56 bytes
// if use BC1 to compress color, can reduce to 4 bytes per cube, 1 / 8 size
// and we can use the active / inactive to adjust lerp value between probe
public struct ALIGN_8 AmbientCube
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

    public uint skyVisibility_pznzpyny;
    public uint skyVisibility_pxnxs0s1;
};

// 16 bytes per voxel
// simplfy scene reprent, matId like Minecraft blockId, skyVisibility and sunVisibility represent a simple gi
// skyVisibility could use to occlution the skylight
// sunVisibility could use to occlution the sun, instead of realtime shadowmap, and could use to render volume effects
// airMatId could use to represent cloud/smoke etc.
public struct ALIGN_16 VoxelData
{
    public uint matId; // 0 is void, that is clean default value
    public uint age;
    public uint distanceToSolid_gg_z01;
    public uint distanceToSolid_x01_y01; // 0.0 - 1.0, 1.0 is full visibility, 0.0 is no visibility
};

// PageIndexer is align with world, 16x16 meter per page
// a 1km map is 64x64 pages, 4096 pages in total
// now is used to boost the dda tracing performance
public struct ALIGN_16 PageIndex
{
    public uint pageId;
    public uint voxelCount;
    public uint voxelDataIdx;
    public uint probeDataIdx;
};

public struct ALIGN_16 SphericalHarmonics
{
    // 3 bands (9 coefficients per color channel)
    public float coefficients[3][9];
    public float padding;
};

public struct ALIGN_16 LightObject
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

public struct ALIGN_16 RayCastContext
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

#ifndef __cplusplus

public struct VkDrawIndexedIndirectCommand {
    public uint indexCount;
    public uint instanceCount;
    public uint firstIndex;
    public int vertexOffset;
    public uint firstInstance;
};

public struct ALIGN_16 Material
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
    public float Reserverd3;
    public float Reserverd4;
};

public struct ALIGN_16 Vertex
{
  public float3 Position;
  public float3 Normal;
  public float4 Tangent;
  public float2 TexCoord;
  public uint MaterialIndex;
  public float3 LocalNormal;
  public float4 LocalTangent;
};

public struct ALIGN_16 GPUVertex
{
    public half4 Position_Tx;
    public half4 Normal_Ty;
    public half4 Tangent;
};
#endif


#ifdef __cplusplus
// still 144bytes, need to shrink into 128 bytes to make full compatible with vulkan
// some static address can be pack together
public struct ALIGN_8 GPUScene
{
    uint64_t Camera;
    uint64_t Nodes;

    uint64_t Reorders;
    uint64_t VerticesSimple;

    uint64_t Vertices;
    uint64_t Indices;

    uint64_t Materials;
    uint64_t Offsets;

    uint64_t Cubes;
    uint64_t Voxels;
    
    uint64_t Pages;
    uint64_t HDRSHs;

    uint64_t Lights;
    uint64_t IndirectDrawCommands;

    uint64_t GPUDrivenStats;
    uint64_t TLAS;

    uint SwapChainIndex;
    uint custom_data_0;
    uint custom_data_1;
    uint custom_data_2;
};

#else

#ifdef PLATFORM_APPLE
public struct ALIGN_8 GPUScene
{
    /* Scene Info */
    public UniformBufferObject *Camera; 

    /* Scene Node Tree */
    public NodeProxy *Nodes;

    public uint *Reorders;
    public half4 *VerticesSimple;
    public GPUVertex *Vertices;
    public uint *Indices;
    public Material *Materials;
    public Material GetMaterial(int idx)
    {
        return Materials[min(MAX_MATERIALS - 1, max(0, idx))];
    }
    public ModelData *Offsets;
    public AmbientCube *Cubes;
    public VoxelData *Voxels;
    public PageIndex *Pages;
    public SphericalHarmonics *HDRSHs;

    public LightObject *Lights;
    public VkDrawIndexedIndirectCommand *IndirectDrawCommands;
   
    public GPUDrivenStat *GPUDrivenStats;
    public uint64_t TLAS;
    public RaytracingAccelerationStructure GetTLAS() {
		return RaytracingAccelerationStructure(TLAS);
	}

    public uint SwapChainIndex;
    public uint custom_data_0;
    public uint custom_data_1;
    public uint custom_data_2;
};
#else
public struct ALIGN_8 GPUScene
{
    /* Scene Info */
    uint64_t2 Camera_Node_Address;
    public property UniformBufferObject* Camera
    {
        get { return (UniformBufferObject*)Camera_Node_Address.x; }
    }

    /* Scene Node Tree */
    public property NodeProxy* Nodes
    {
        get { return (NodeProxy*)Camera_Node_Address.y; }
    }

    /* Global Vertice Buffer */
    uint64_t2 Reorders_VerticesSimple_Address;
    public property uint* Reorders
    {
        get { return (uint*)Reorders_VerticesSimple_Address.x; }
    }
    public property half4* VerticesSimple
    {
        get { return (half4*)Reorders_VerticesSimple_Address.y; }
    }
    
    uint64_t2 Vertices_Indices_Address;
    public property GPUVertex* Vertices
    {
        get { return (GPUVertex*)Vertices_Indices_Address.x; }
    }
    public property uint* Indices
    {
        get { return (uint*)Vertices_Indices_Address.y; }
    }

    /* Resources */
    uint64_t2 Materials_OffsetsAddress;
    public property Material* Materials
    {
        get { return (Material*)Materials_OffsetsAddress.x; }
    }
    public Material GetMaterial(int idx)
    {
        return Materials[min(MAX_MATERIALS - 1, max(0, idx))];
    }
    public property ModelData* Offsets
    {
        get { return (ModelData*)Materials_OffsetsAddress.y; }
    }

    /* Others */
    uint64_t2 Cubes_Voxels_Address;
    public property AmbientCube* Cubes
    {
        get { return (AmbientCube*)Cubes_Voxels_Address.x; }
    }
    public property VoxelData* Voxels
    {
        get { return (VoxelData*)Cubes_Voxels_Address.y; }
    }
    
    uint64_t2 Pages_HDRSHs_Address;
    public property PageIndex* Pages
    {
        get { return (PageIndex*)Pages_HDRSHs_Address.x; }
    }
    public property SphericalHarmonics* HDRSHs
    {
        get { return (SphericalHarmonics*)Pages_HDRSHs_Address.y; }
    }

    uint64_t2 Lights_IndirectDrawCommands_Address;
    public property LightObject* Lights
    {
        get { return (LightObject*)Lights_IndirectDrawCommands_Address.x; }
    }
    public property VkDrawIndexedIndirectCommand* IndirectDrawCommands
    {
        get { return (VkDrawIndexedIndirectCommand*)Lights_IndirectDrawCommands_Address.y; }
    }
    
    uint64_t2 GPUDrivenStats_TLAS_Address;
    public property GPUDrivenStat* GPUDrivenStats
    {
        get { return (GPUDrivenStat*)GPUDrivenStats_TLAS_Address.x; }
    }

    public RaytracingAccelerationStructure GetTLAS() {
#ifdef PLATFORM_ANDROID
        return BindedTLAS;
#else
        return RaytracingAccelerationStructure(GPUDrivenStats_TLAS_Address.y);
#endif
	}

    public uint SwapChainIndex;
    public uint custom_data_0;
    public uint custom_data_1;
    public uint custom_data_2;
};
#ifdef PLATFORM_ANDROID
[[vk::binding(0, 1)]] RaytracingAccelerationStructure BindedTLAS;
#endif
#endif // PLATFORM_APPLE

#endif

#ifdef __cplusplus
#undef ALIGN_16
#undef ALIGN_8
#else

#endif