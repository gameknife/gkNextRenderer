#ifdef __cplusplus
#define ALIGN_16 alignas(16) 
#define ALIGN_8 alignas(8)
#else
#define ALIGN_16
#define ALIGN_8
implementing Common;
#endif

public static const int MAX_NODES = 65535; // 256;
public static const int MAX_MATERIALS = 16384;
public static const int MAX_HDR_SH = 100;

public static const int HDR_OUTPUT_MODE_SDR = 0;
public static const int HDR_OUTPUT_MODE_HDR10_ST2084 = 1;
public static const int HDR_OUTPUT_MODE_EXTENDED_SRGB_LINEAR = 2;

public static const int GPU_SCENE_NODE_PROXY_SIZE = 224;
public static const int GPU_SCENE_MATERIAL_SIZE = 64;
public static const int GPU_SCENE_GPU_DRIVEN_STAT_SIZE = 32;
public static const int GPU_SCENE_SHADOW_GPU_DRIVEN_STAT_COUNT = 4;
public static const int GPU_SCENE_SPHERICAL_HARMONICS_SIZE = 112;

public static const int GPU_SCENE_DYNAMIC_NODES_OFFSET = 0;
public static const int GPU_SCENE_DYNAMIC_MATERIALS_OFFSET =
    GPU_SCENE_DYNAMIC_NODES_OFFSET + GPU_SCENE_NODE_PROXY_SIZE * MAX_NODES;
public static const int GPU_SCENE_DYNAMIC_GPU_DRIVEN_STATS_OFFSET =
    GPU_SCENE_DYNAMIC_MATERIALS_OFFSET + GPU_SCENE_MATERIAL_SIZE * MAX_MATERIALS;
public static const int GPU_SCENE_DYNAMIC_SHADOW_GPU_DRIVEN_STATS_OFFSET =
    GPU_SCENE_DYNAMIC_GPU_DRIVEN_STATS_OFFSET + GPU_SCENE_GPU_DRIVEN_STAT_SIZE;
public static const int GPU_SCENE_DYNAMIC_HDRSHS_OFFSET =
    GPU_SCENE_DYNAMIC_SHADOW_GPU_DRIVEN_STATS_OFFSET +
    GPU_SCENE_GPU_DRIVEN_STAT_SIZE * GPU_SCENE_SHADOW_GPU_DRIVEN_STAT_COUNT;
public static const int GPU_SCENE_DYNAMIC_SIZE =
    GPU_SCENE_DYNAMIC_HDRSHS_OFFSET + GPU_SCENE_SPHERICAL_HARMONICS_SIZE * MAX_HDR_SH;

public static const int GPU_SCENE_AMBIENT_PER_CASCADE_COUNT = 192 * 192 * 48;
public static const int GPU_SCENE_AMBIENT_CASCADE_MAX = 4;
public static const int GPU_SCENE_AMBIENT_CUBE_SIZE = 40;
public static const int GPU_SCENE_VOXEL_DATA_SIZE = 16;
public static const int GPU_SCENE_PAGE_INDEX_SIZE = 16;
public static const int GPU_SCENE_AMBIENT_SEED_SIZE = 16;
public static const int GPU_SCENE_ACGI_PAGE_COUNT = 64;

public static const int GPU_SCENE_AMBIENT_CUBES_OFFSET = 0;
public static const int GPU_SCENE_AMBIENT_VOXELS_OFFSET =
    GPU_SCENE_AMBIENT_CUBES_OFFSET +
    GPU_SCENE_AMBIENT_CUBE_SIZE * GPU_SCENE_AMBIENT_PER_CASCADE_COUNT * GPU_SCENE_AMBIENT_CASCADE_MAX;
public static const int GPU_SCENE_AMBIENT_PAGES_OFFSET =
    GPU_SCENE_AMBIENT_VOXELS_OFFSET +
    GPU_SCENE_VOXEL_DATA_SIZE * GPU_SCENE_AMBIENT_PER_CASCADE_COUNT * GPU_SCENE_AMBIENT_CASCADE_MAX;
public static const int GPU_SCENE_AMBIENT_CUBES_PONG_OFFSET =
    GPU_SCENE_AMBIENT_PAGES_OFFSET +
    GPU_SCENE_PAGE_INDEX_SIZE * GPU_SCENE_ACGI_PAGE_COUNT * GPU_SCENE_ACGI_PAGE_COUNT;
public static const int GPU_SCENE_AMBIENT_SIZE =
    GPU_SCENE_AMBIENT_CUBES_PONG_OFFSET +
    GPU_SCENE_AMBIENT_CUBE_SIZE * GPU_SCENE_AMBIENT_PER_CASCADE_COUNT;

// Phase 3 sparse brick storage. The cube grid (192x192x48 per cascade) is tiled into 8^3 bricks;
// only bricks near surfaces are allocated in the cube pool, indexed via a per-cascade brick table.
// 192/8 = 24 (x,z), 48/8 = 6 (y) -> 24*6*24 = 3456 bricks/cascade, 3456*512 = 1,769,472 = per-cascade.
public static const int GPU_SCENE_AMBIENT_BRICK_EDGE = 8;
public static const int GPU_SCENE_AMBIENT_BRICK_VOLUME = 512; // 8*8*8
public static const int GPU_SCENE_AMBIENT_BRICKS_X = 24;
public static const int GPU_SCENE_AMBIENT_BRICKS_Y = 6;
public static const int GPU_SCENE_AMBIENT_BRICKS_Z = 24;
public static const int GPU_SCENE_AMBIENT_BRICKS_PER_CASCADE = 3456; // BRICKS_X * BRICKS_Y * BRICKS_Z
public static const uint GPU_SCENE_AMBIENT_BRICK_INVALID = 0xffffffffu;

public struct ALIGN_16 GPUDrivenStat
{
    public uint ProcessedCount;
    public uint VisibleCount;
    public uint CulledCount;
    public uint TriangleCount;
    public uint CulledTriangleCount;
    public uint ReservedAlign1;
    public uint ReservedAlign2;
    public uint ReservedAlign3;
};

public struct ALIGN_16 SoftMeshShaderVisibleItem
{
    public uint primBase;
    public uint instanceIdx;
    public uint modelOffsetIdx;
    public uint triCount;
};

public struct ALIGN_16 SoftMeshShaderResources
{
    public uint64_t Prim;
    public uint64_t ShadowPrim;
    public uint64_t VisibleItems;
    public uint64_t DrawArgs;
    public uint64_t DispatchArgs;
    public uint64_t Counters;
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
    public float4x4 ProjectionUnJit;
    public float4x4 ProjectionInverseUnJit;

    public float4 Jitter;
    public float4 ViewportRect;
    public float4 SunDirection;
    public float4 SunColor;

    public float4x4 SunCascadeViewProjection[4];
    public float4 CascadeSplits;      // view-space positive distance at the far edge of each cascade

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
    public uint LightCount;

    public bool HasSky;
    public bool ShowHeatmap;
    public uint TemporalFrames;
    public bool HasSun;

    public bool HDR;
    public uint HDROutputMode;
    public bool TAA;
    public bool ProgressiveRender;
    public bool FastGather;

    public bool DebugDraw_Lighting;
    public bool DebugDraw_ShadowCascadeCoverage;
    public uint SuperResolution;
    public float SceneEpsilonScale;

    public float AmbientCubeUnit;
    public float AmbientCubeOffsetPadding0;
    public float AmbientCubeOffsetPadding1;
    public float AmbientCubeOffsetPadding2;

    public float3 AmbientCubeOffset;
    public float AmbientCubeOffsetPadding3;

    public float4 AmbientCubeCascadeParams;

    // Denoiser: bindless slots holding the final diffuse / specular the compose pass should read
    // (a-trous outputs when enabled, else the temporal accumulation buffers).
    public uint DenoiseDiffuseSourceSlot;
    public uint DenoiseSpecularSourceSlot;

    // SwModernNoAmbient GTAO. Kept in the camera UBO so both GTAO compute stages can
    // use the regular GPUScene push constant without introducing another pipeline ABI.
    public float GTAORadius;
    public float GTAOStrength;
    public float GTAOThickness;
    public uint GTAODebugMode;

    public bool GTAOEnable;
    public uint GTAOQuality;
    public uint GTAOPadding1;
    public uint GTAOPadding2;

    // SwModernNoAmbient voxel sky-visibility (large-scale / off-screen occlusion, orthogonal to GTAO).
    // Sampled in the shading pass (written to RT_AMBIENT.a) and combined with GTAO in GTAOCompose.
    public bool SkyVisEnable;
    public float SkyVisStrength;     // 0 = no darkening, 1 = full baked occlusion
    public float SkyVisMaxDistance;  // soft-trace max distance (meters); large-scale range
    public uint SkyVisRayCount;      // hemisphere rays per voxel for the bake

    public uint SkyVisCombineMode;   // 0 = multiply, 1 = min(ao, skyVis), 2 = near-bright
    public uint SkyVisBlurRadius;    // bilateral blur radius (pixels) applied to skyVis in compose; 0 = off
    public float SkyVisJitterRadius; // random normal-hemisphere sample jitter radius in voxel units; 0 = off
    public uint SkyVisPadding2;
};

public struct ALIGN_16 NodeProxy
{
    public uint instanceId;
    public uint modelId;
    public uint skinId;
    public uint jointMatrixOffset;

    public float4x4 worldTS;
    public float4x4 combinedPrevTS;

    public uint matId[16];
    
    public uint visible;
    public uint nort;
    public uint reserved1;
    public uint reserved2;
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

// size = 10 x 4 bytes, 40 bytes
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
 
    public uint SunDirect;
    public uint SkyEmissiveDirect;
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

public struct ALIGN_16 AmbientBrickResidency
{
    public uint lastConsumerHitFrame;
    public uint lastBounceHitFrame;
    public uint consumerHitCount;
    public uint bounceHitCount;
};

// Ambient GI resource table. GPUScene is capped at 128B (push constant), so instead of inlining
// every ambient region address it carries a single AmbientBase pointer to this table. Region
// addresses are filled CPU-side (Scene.cpp) from the arena base, which decouples the GPU-visible
// layout from the compile-time GPU_SCENE_AMBIENT_*_OFFSET constants and lets each region be sized
// or relocated independently (right-size cascades, sparse brick pool).
public struct ALIGN_16 AmbientResources
{
    public uint64_t Cubes;       // brick pool base (Phase 3) / dense cube array (pre-3a)
    public uint64_t Voxels;      // dense voxel array (kept dense; Phase 4 may sparsify)
    public uint64_t Pages;
    public uint64_t CubesPong;   // ping-pong copy of the baking cascade's cubes (brick-organized in Phase 3)
    public uint64_t BrickTable;  // Phase 3: per-cascade brick -> pool slot table (uint per brick)
    public uint64_t PoolParams;  // Phase 3: low32 = pool bricks/cascade, high32 = active-list byte offset from BrickTable
    public uint64_t Residency;   // per-cascade AmbientBrickResidency array
    public uint64_t Reserved0;
    public uint64_t Reserved1;
    public uint64_t Reserved2;
};

public struct ALIGN_8 SharcHashEntry
{
    public uint64_t Key;
};

public struct ALIGN_16 SharcAccumulationEntry
{
    public uint DataX;
    public uint DataY;
    public uint DataZ;
    public uint DataW;
};

public struct ALIGN_16 SharcResolvedEntry
{
    public uint64_t RadianceData;
    public uint SampleData;
    public uint SampleDataExt;
};

public struct ALIGN_16 SharcRuntimeParameters
{
    public uint EntryCount;
    public uint FrameIndex;
    public uint DebugMode;
    public uint QueryMinBounce;
    public float SceneScale;
    public float LevelBias;
    public float RadianceScale;
    public float UpdateSampleRatio;
    public float QueryRoughnessMin;
    public uint AccumulatedFrameMax;
    public uint ResponsiveFrameMax;
    public uint StaleFrameMax;
    public float4 CameraPositionPrev;
};

public struct ALIGN_16 SharcResources
{
    public uint64_t HashEntries;
    public uint64_t LockBuffer;
    public uint64_t Accumulation;
    public uint64_t Resolved;
    public uint64_t Parameters;
    public uint64_t Reserved0;
    public uint64_t Reserved1;
    public uint64_t Reserved2;
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

public struct VkDrawIndirectCommand {
    public uint vertexCount;
    public uint instanceCount;
    public uint firstVertex;
    public uint firstInstance;
};

public struct VkDispatchIndirectCommand {
    public uint x;
    public uint y;
    public uint z;
};

public struct ALIGN_16 Material
{
    public float4 Diffuse;

    public int DiffuseTextureId;
    public int MRATextureId;
    public int NormalTextureId;
    public int EmissiveTextureId;
    
    public float Fuzziness;
    public float RefractionIndex;
    public uint MaterialModel;
    public float Metalness;
    
    public float RefractionIndex2;
    public float NormalTextureScale;
    public float Reserverd2;
    public float Reserverd3;
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
// 14 addresses + 4 uint params; aligned size is 128 bytes for Vulkan push_constant.
public struct ALIGN_16 GPUScene
{
    // Unified push-constant header: kept FIRST so it aliases the header of every custom push
    // constant (see ZeroBindCustomPushConstantPipeline). custom_data_0 == active RenderView RT
    // bank base, readable via gpuScene.custom_data_0 in *all* passes regardless of which pipeline.
    uint32_t SwapChainIndex;
    uint32_t custom_data_0;
    uint32_t custom_data_1;
    uint32_t custom_data_2;

    uint64_t Camera;
    uint64_t SceneDynamicBase;
    uint64_t Reorders;
    uint64_t Vertices;
    uint64_t Indices;
    uint64_t Offsets;
    uint64_t ReservedAddress0;
    uint64_t AmbientBase;
    uint64_t TLAS;

    uint64_t SkinWeights;
    uint64_t SkinJoints;
    uint64_t SkinnedVertices;
    uint64_t JointMatrices;
    uint64_t SoftMeshShaderResourcesAddress;
};

#else

#ifdef PLATFORM_APPLE
public struct ALIGN_8 GPUScene
{
    // Unified push-constant header (FIRST; aliases every custom push constant's header).
    public uint SwapChainIndex;
    public uint custom_data_0;
    public uint custom_data_1;
    public uint custom_data_2;

    /* Scene Info */
    public UniformBufferObject *Camera;
    public uint64_t SceneDynamicBase;

    /* Scene Node Tree */
    public property NodeProxy* Nodes
    {
        get { return (NodeProxy*)(SceneDynamicBase + GPU_SCENE_DYNAMIC_NODES_OFFSET); }
    }

    public uint *Reorders;
    public GPUVertex *Vertices;
    public uint *Indices;
    public property Material* Materials
    {
        get { return (Material*)(SceneDynamicBase + GPU_SCENE_DYNAMIC_MATERIALS_OFFSET); }
    }
    public Material GetMaterial(int idx)
    {
        return Materials[min(MAX_MATERIALS - 1, max(0, idx))];
    }
    public ModelData *Offsets;
    public uint64_t ReservedAddress0;

    public uint64_t AmbientBase; // -> AmbientResources table (see BasicTypes.slang AmbientResources)
    public property SharcResources* Sharc
    {
        get { return (SharcResources*)ReservedAddress0; }
    }
    public property AmbientCube* Cubes
    {
        get { return (AmbientCube*)((AmbientResources*)AmbientBase)[0].Cubes; }
    }
    public property VoxelData* Voxels
    {
        get { return (VoxelData*)((AmbientResources*)AmbientBase)[0].Voxels; }
    }
    public property PageIndex* Pages
    {
        get { return (PageIndex*)((AmbientResources*)AmbientBase)[0].Pages; }
    }
    public property AmbientCube* CubesPong
    {
        get { return (AmbientCube*)((AmbientResources*)AmbientBase)[0].CubesPong; }
    }
    public property uint* AmbientBrickTable
    {
        get { return (uint*)((AmbientResources*)AmbientBase)[0].BrickTable; }
    }
    public property uint* AmbientActiveBrickList
    {
        get
        {
            uint64_t params = ((AmbientResources*)AmbientBase)[0].PoolParams;
            return (uint*)(((AmbientResources*)AmbientBase)[0].BrickTable + (params >> 32));
        }
    }
    public property uint AmbientPoolBricksPerCascade
    {
        get { return uint(((AmbientResources*)AmbientBase)[0].PoolParams & 0xFFFFFFFF); }
    }
    public property AmbientBrickResidency* AmbientResidency
    {
        get { return (AmbientBrickResidency*)((AmbientResources*)AmbientBase)[0].Residency; }
    }
    public property SphericalHarmonics* HDRSHs
    {
        get { return (SphericalHarmonics*)(SceneDynamicBase + GPU_SCENE_DYNAMIC_HDRSHS_OFFSET); }
    }

    public property GPUDrivenStat* GPUDrivenStats
    {
        get { return (GPUDrivenStat*)(SceneDynamicBase + GPU_SCENE_DYNAMIC_GPU_DRIVEN_STATS_OFFSET); }
    }
    public property GPUDrivenStat* ShadowGPUDrivenStats
    {
        get { return (GPUDrivenStat*)(SceneDynamicBase + GPU_SCENE_DYNAMIC_SHADOW_GPU_DRIVEN_STATS_OFFSET); }
    }
    public uint64_t TLAS;

    public float4 *SkinWeights;
    public uint4 *SkinJoints;

    public GPUVertex *SkinnedVertices;
    public float4x4 *JointMatrices;
    public uint64_t SoftMeshShaderResourcesAddress;
    public property SoftMeshShaderResources* SoftMeshShader
    {
        get { return (SoftMeshShaderResources*)SoftMeshShaderResourcesAddress; }
    }

    public RaytracingAccelerationStructure GetTLAS() {
		return RaytracingAccelerationStructure(TLAS);
	}
};
#else
public struct ALIGN_8 GPUScene
{
    // Unified push-constant header (FIRST; aliases every custom push constant's header).
    public uint32_t SwapChainIndex;
    public uint32_t custom_data_0;
    public uint32_t custom_data_1;
    public uint32_t custom_data_2;

    /* Scene Info */
    uint64_t2 Camera_SceneDynamicBase_Address;
    public property UniformBufferObject* Camera
    {
        get { return (UniformBufferObject*)Camera_SceneDynamicBase_Address.x; }
    }

    /* Scene Node Tree */
    public property NodeProxy* Nodes
    {
        get { return (NodeProxy*)(Camera_SceneDynamicBase_Address.y + GPU_SCENE_DYNAMIC_NODES_OFFSET); }
    }

    /* Global Vertice Buffer */
    uint64_t2 Reorders_Vertices_Address;
    public property uint* Reorders
    {
        get { return (uint*)Reorders_Vertices_Address.x; }
    }
    public property GPUVertex* Vertices
    {
        get { return (GPUVertex*)Reorders_Vertices_Address.y; }
    }

    uint64_t2 Indices_Offsets_Address;
    public property uint* Indices
    {
        get { return (uint*)Indices_Offsets_Address.x; }
    }

    /* Resources */
    public property Material* Materials
    {
        get { return (Material*)(Camera_SceneDynamicBase_Address.y + GPU_SCENE_DYNAMIC_MATERIALS_OFFSET); }
    }
    public Material GetMaterial(int idx)
    {
        return Materials[min(MAX_MATERIALS - 1, max(0, idx))];
    }
    public property ModelData* Offsets
    {
        get { return (ModelData*)Indices_Offsets_Address.y; }
    }

    /* Others */
    uint64_t2 Reserved_AmbientBase_Address;

    public property SharcResources* Sharc
    {
        get { return (SharcResources*)Reserved_AmbientBase_Address.x; }
    }

    public property AmbientCube* Cubes
    {
        get { return (AmbientCube*)((AmbientResources*)Reserved_AmbientBase_Address.y)[0].Cubes; }
    }
    public property VoxelData* Voxels
    {
        get { return (VoxelData*)((AmbientResources*)Reserved_AmbientBase_Address.y)[0].Voxels; }
    }

    public property PageIndex* Pages
    {
        get { return (PageIndex*)((AmbientResources*)Reserved_AmbientBase_Address.y)[0].Pages; }
    }
    public property SphericalHarmonics* HDRSHs
    {
        get { return (SphericalHarmonics*)(Camera_SceneDynamicBase_Address.y + GPU_SCENE_DYNAMIC_HDRSHS_OFFSET); }
    }

    public property AmbientCube* CubesPong
    {
        get { return (AmbientCube*)((AmbientResources*)Reserved_AmbientBase_Address.y)[0].CubesPong; }
    }
    public property uint* AmbientBrickTable
    {
        get { return (uint*)((AmbientResources*)Reserved_AmbientBase_Address.y)[0].BrickTable; }
    }
    public property uint* AmbientActiveBrickList
    {
        get
        {
            uint64_t params = ((AmbientResources*)Reserved_AmbientBase_Address.y)[0].PoolParams;
            return (uint*)(((AmbientResources*)Reserved_AmbientBase_Address.y)[0].BrickTable + (params >> 32));
        }
    }
    public property uint AmbientPoolBricksPerCascade
    {
        get { return uint(((AmbientResources*)Reserved_AmbientBase_Address.y)[0].PoolParams & 0xFFFFFFFF); }
    }
    public property AmbientBrickResidency* AmbientResidency
    {
        get { return (AmbientBrickResidency*)((AmbientResources*)Reserved_AmbientBase_Address.y)[0].Residency; }
    }

    uint64_t2 TLAS_SkinWeights_Address;
    public property GPUDrivenStat* GPUDrivenStats
    {
        get { return (GPUDrivenStat*)(Camera_SceneDynamicBase_Address.y + GPU_SCENE_DYNAMIC_GPU_DRIVEN_STATS_OFFSET); }
    }
    public property GPUDrivenStat* ShadowGPUDrivenStats
    {
        get
        {
            return (GPUDrivenStat*)(Camera_SceneDynamicBase_Address.y +
                                    GPU_SCENE_DYNAMIC_SHADOW_GPU_DRIVEN_STATS_OFFSET);
        }
    }

    public property float4* SkinWeights
    {
        get { return (float4*)TLAS_SkinWeights_Address.y; }
    }

    uint64_t2 SkinJoints_SkinnedVertices_Address;
    public property uint4* SkinJoints
    {
        get { return (uint4*)SkinJoints_SkinnedVertices_Address.x; }
    }

    public property GPUVertex* SkinnedVertices
    {
        get { return (GPUVertex*)SkinJoints_SkinnedVertices_Address.y; }
    }

    uint64_t2 JointMatrices_Reserved_Address;
    public property float4x4* JointMatrices
    {
        get { return (float4x4*)JointMatrices_Reserved_Address.x; }
    }
    public property SoftMeshShaderResources* SoftMeshShader
    {
        get { return (SoftMeshShaderResources*)JointMatrices_Reserved_Address.y; }
    }

    public RaytracingAccelerationStructure GetTLAS() {
#ifdef PLATFORM_ANDROID
        return BindedTLAS;
#else
        return RaytracingAccelerationStructure(TLAS_SkinWeights_Address.x);
#endif
	}
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
