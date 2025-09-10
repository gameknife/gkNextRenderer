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
public struct ALIGN_8 GPUScene
{
    public uint64_t Camera;
    /* Scene Node Tree */
    public uint64_t Nodes;

    /* Global Vertice Buffer */
    public uint64_t Reorders;
    public uint64_t VerticesSimple;
    public uint64_t Vertices;
    public uint64_t Indices;

    /* Resources */
    public uint64_t Materials;
    public uint64_t Offsets;

    /* Others */
    public uint64_t Cubes;
    public uint64_t Voxels;
    public uint64_t Pages;
    public uint64_t HDRSHs;
    public uint64_t Lights;

    public uint64_t IndirectDrawCommands;
    public uint64_t GPUDrivenStats;

    public uint64_t TLAS;

    public uint SwapChainIndex;
    public uint custom_data_0;
    public uint custom_data_1;
    public uint custom_data_2;
};

#else
public struct ALIGN_8 GPUScene
{
    /* Scene Info */
    uint64_t CameraAddress;
    public property UniformBufferObject* Camera
    {
        get { return (UniformBufferObject*)CameraAddress; }
    }

    /* Scene Node Tree */
    uint64_t NodesAddress;
    public property NodeProxy* Nodes
    {
        get { return (NodeProxy*)NodesAddress; }
    }

    /* Global Vertice Buffer */
    uint64_t ReordersAddress;
    public property uint* Reorders
    {
        get { return (uint*)ReordersAddress; }
    }
    
    uint64_t VerticesSimpleAddress;
    public property half4* VerticesSimple
    {
        get { return (half4*)VerticesSimpleAddress; }
    }
    
    uint64_t VerticesAddress;
    public property GPUVertex* Vertices
    {
        get { return (GPUVertex*)VerticesAddress; }
    }
    
    uint64_t IndicesAddress;
    public property uint* Indices
    {
        get { return (uint*)IndicesAddress; }
    }

    /* Resources */
    uint64_t MaterialsAddress;
    public property Material* Materials
    {
        get { return (Material*)MaterialsAddress; }
    }
    
    public Material GetMaterial(int idx)
    {
        return Materials[min(MAX_MATERIALS - 1, max(0, idx))];
    }
    
    uint64_t OffsetsAddress;
    public property ModelData* Offsets
    {
        get { return (ModelData*)OffsetsAddress; }
    }

    /* Others */
    uint64_t CubesAddress;
    public property AmbientCube* Cubes
    {
        get { return (AmbientCube*)CubesAddress; }
    }
    
    uint64_t VoxelsAddress;
    public property VoxelData* Voxels
    {
        get { return (VoxelData*)VoxelsAddress; }
    }
    
    uint64_t PagesAddress;
    public property PageIndex* Pages
    {
        get { return (PageIndex*)PagesAddress; }
    }
    
    uint64_t HDRSHsAddress;
    public property SphericalHarmonics* HDRSHs
    {
        get { return (SphericalHarmonics*)HDRSHsAddress; }
    }
    
    uint64_t LightsAddress;
    public property LightObject* Lights
    {
        get { return (LightObject*)LightsAddress; }
    }

    /* IndirectDraw */
    uint64_t IndirectDrawCommandsAddress;
    public property VkDrawIndexedIndirectCommand* IndirectDrawCommands
    {
        get { return (VkDrawIndexedIndirectCommand*)IndirectDrawCommandsAddress; }
    }
    
    uint64_t GPUDrivenStatsAddress;
    public property GPUDrivenStat* GPUDrivenStats
    {
        get { return (GPUDrivenStat*)GPUDrivenStatsAddress; }
    }
    
    public uint64_t TLAS; 
    public RaytracingAccelerationStructure GetTLAS() {
		return spirv_asm {
			OpExtension "SPV_KHR_ray_tracing";
			OpCapability RayTracingKHR;
			result: $$RaytracingAccelerationStructure = OpConvertUToAccelerationStructureKHR $TLAS;
		};
	}

    public uint SwapChainIndex;
    public uint custom_data_0;
    public uint custom_data_1;
    public uint custom_data_2;

};

#endif

#ifdef __cplusplus
#undef ALIGN_16
#undef ALIGN_8
#else

#endif