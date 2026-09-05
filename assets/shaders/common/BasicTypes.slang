#include "Atmosphere.slang"

#ifdef __cplusplus
#define ALIGN_16 alignas(16) 
#define ALIGN_8 alignas(8)
#else
#define ALIGN_16
#define ALIGN_8
#endif

// RenderComponent sections are expanded into one NodeProxy each. Visibility IDs are one-based,
// with zero reserved for the background.
public static const int MAX_RENDER_PROXIES = 131072;
public static const int MAX_MATERIALS = 16384;
public static const int MAX_HDR_SH = 100;

// Discrete LOD levels per model section, including LOD0. Every section owns exactly this many
// consecutive ModelLod entries so a shader can index by level without a per-model table offset.
public static const int MAX_MODEL_LOD_COUNT = 4;

// Per-cascade sun shadow map edge. Shared so the shadow cull pass can size a proxy's projected
// extent in shadow-map pixels using the same number the pass actually renders at.
public static const int SUN_SHADOW_RESOLUTION = 1024;

public static const int HDR_OUTPUT_MODE_SDR = 0;
public static const int HDR_OUTPUT_MODE_HDR10_ST2084 = 1;
public static const int HDR_OUTPUT_MODE_EXTENDED_SRGB_LINEAR = 2;

public static const uint BACKGROUND_MODE_ENVIRONMENT = 0u;
public static const uint BACKGROUND_MODE_STUDIO = 1u;

public static const uint RENDER_PARTICIPATION_MAIN_VISIBILITY = 1u << 0;
public static const uint RENDER_PARTICIPATION_SHADOW_CASTER = 1u << 1;
public static const uint RENDER_PARTICIPATION_GPU_AS = 1u << 2;
public static const uint RENDER_PARTICIPATION_GI_BAKE = 1u << 3;

public static const int GPU_SCENE_NODE_PROXY_SIZE = 224;
public static const int GPU_SCENE_MATERIAL_SIZE = 64;
public static const int GPU_SCENE_GPU_DRIVEN_STAT_SIZE = 32;
public static const int GPU_SCENE_SHADOW_GPU_DRIVEN_STAT_COUNT = 4;
public static const int GPU_SCENE_SPHERICAL_HARMONICS_SIZE = 112;

public static const int GPU_SCENE_DYNAMIC_NODES_OFFSET = 0;
public static const int GPU_SCENE_DYNAMIC_MATERIALS_OFFSET =
    GPU_SCENE_DYNAMIC_NODES_OFFSET + GPU_SCENE_NODE_PROXY_SIZE * MAX_RENDER_PROXIES;
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
public static const int GPU_SCENE_VOXEL_DATA_SIZE = 8;
public static const int GPU_SCENE_PAGE_INDEX_SIZE = 16;
public static const int GPU_SCENE_AMBIENT_SEED_SIZE = 16;
public static const int GPU_SCENE_ACGI_PAGE_COUNT = 64;

// Cascaded world-space light grid. Every light-sampling path resolves "which lights matter at this
// world position" through it, so it must be world-space: a view-frustum cluster grid cannot answer
// for path-tracer bounce points, SHARC cache updates or ambient-cube baking, which is exactly what
// would force each renderer back onto its own light loop.
// Cascades are camera-anchored with a geometric cell-size ratio; a position outside every cascade
// falls back to the global CDF, so the structure is an accelerator and never a correctness gate.
public static const int GPU_SCENE_LIGHT_GRID_SIZE_XZ = 32;
public static const int GPU_SCENE_LIGHT_GRID_SIZE_Y = 16;
public static const int GPU_SCENE_LIGHT_GRID_CASCADE_MAX = 3;
public static const float GPU_SCENE_LIGHT_GRID_CASCADE_RATIO = 4.0f;
// Per-cell capacity. Overflow keeps the most important lights and drops the rest: unbiased for the
// path tracer (it divides by the pdf actually used) but a genuine approximation for forward paths.
public static const int GPU_SCENE_LIGHT_GRID_CAPACITY = 32;
public static const int GPU_SCENE_LIGHT_GRID_CASCADE_CELLS =
    GPU_SCENE_LIGHT_GRID_SIZE_XZ * GPU_SCENE_LIGHT_GRID_SIZE_Y * GPU_SCENE_LIGHT_GRID_SIZE_XZ;
public static const int GPU_SCENE_LIGHT_GRID_CELLS =
    GPU_SCENE_LIGHT_GRID_CASCADE_CELLS * GPU_SCENE_LIGHT_GRID_CASCADE_MAX;
// Layout: [uint count per cell][uint entry per cell slot]. An entry packs the light index in the
// low 16 bits and the unorm16 CDF upper bound in the high 16, so a selection walk reads one dword.
public static const int GPU_SCENE_LIGHT_GRID_ENTRY_OFFSET = GPU_SCENE_LIGHT_GRID_CELLS * 4;
public static const int GPU_SCENE_LIGHT_GRID_SIZE =
    GPU_SCENE_LIGHT_GRID_ENTRY_OFFSET + GPU_SCENE_LIGHT_GRID_CELLS * GPU_SCENE_LIGHT_GRID_CAPACITY * 4;

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
    public uint MaxVisibleProxyIndex;
    // What TriangleCount would have been with every proxy pinned to LOD0. The difference between
    // the two is the geometry discrete LOD removed this frame, which is otherwise invisible: a
    // scene that halves its triangles looks identical to one that simply culled more.
    public uint Lod0TriangleCount;
    // Frame index the cull pass stamped when it populated this block. The reset that precedes the
    // pass zeroes it, so a CPU reader that sees 0 knows it caught the window between the reset and
    // the accumulation and must keep its previous values instead of reporting an empty frame. The
    // readback is deliberately fenceless, so that window is reachable.
    public uint FrameStamp;
};

// Logical visibility identity shared by the expanded primitive stream and visibility consumers.
// Pixel storage uses two physical planes (R32_UINT instance + R16_UINT triangle), while primitive
// records intentionally stay two uints to avoid 16-bit storage-buffer feature/alignment branches.
public struct ALIGN_8 VisibilityId
{
    public uint instanceIdx;
    public uint triangleIdx;
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
    public uint64_t Lights;
    public uint64_t LightGrid;
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
    public float4 SkyColor;

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
    public bool PrimaryRayJitter;
    public bool ProgressiveRender;
    public bool ExitAfterFirst;

    public bool DebugDraw_Lighting;
    public bool ForceBlackBackground;
    public uint SuperResolution;
    public float SceneEpsilonScale;

    public float AmbientCubeUnit;
    // Artistic multiplier on the indirect (post-first-bounce) term only: the SHARC cache hit and
    // the ambient-cube path terminal. Direct lighting and sky are untouched, so raising it lifts
    // bounce light without the contrast blowout that comes from raising SunIntensity. 1 = physical.
    public float IndirectIntensity;
    // Per-order weight on bounces beyond the first. A SHARC entry's radiance is
    // direct + T1*direct(next) + T1*T2*direct(next2) + ...; only the leading term skips every
    // segment throughput, so weighting throughput scales bounce order n by this^(n-1) and
    // leaves once-bounced direct light alone. 1 = physical.
    public float MultiBounceIntensity;
    public float AmbientCubeOffsetPadding2;

    public float3 AmbientCubeOffset;
    public float AmbientCubeOffsetPadding3;

    public float4 AmbientCubeCascadeParams;

    public bool UpscalerInvalidMotionMask;
    public bool RayReconstruction;
    public float GTAORadius;
    public float GTAOStrength;

    public float GTAOThickness;
    public uint GTAODebugMode;
    public bool GTAOEnable;
    public uint GTAOQuality;
    public bool LightObjectScreenSpaceShadow;
    public float LightObjectShadowDistance; // world units the screen-space light shadow marches
    public uint LightObjectMaxShadowedLights;
    public uint LightObjectShadowSteps;

    public uint LightGridCascadeCount;   // 0 disables the grid: every query falls back to the global CDF
    public float LightGridCullThreshold; // minimum per-cell contribution a light must reach to be stored
    public float4 LightGridAnchor;       // xyz = cascade anchor (snapped camera), w = cascade 0 cell size

    // Stable extension point for procedural atmosphere. Zero preserves the legacy IBL path.
    public uint64_t AtmosphereParams;
    public uint64_t AtmosphereReserved0;

    // Checkerboard lighting is handed to the temporal upscaler sparse: the missing parity is left
    // unwritten instead of being reconstructed by a neighbour copy, and Native TAAU fills it from
    // history. Every pass between Core Shading and the upscaler must then respect the shading rate,
    // so this flag lives in the camera UBO rather than in one pass's CustomData word.
    // Appended at the end deliberately: inserting into the middle of this struct shifts every
    // following field, and C++ and Slang do not have to agree on how a bool is packed.
    public bool CheckerboardSparseLighting;

    // The visible camera background is independent from sky lighting: Studio keeps SkyIdx for
    // irradiance/specular/path-tracing misses while replacing only the primary-ray presentation.
    public uint BackgroundMode;

    // Projected pixel extent at or above which a proxy draws at LOD0; each further level halves it.
    // 0 pins everything to LOD0. Lives here rather than in a pass CustomData word because all three
    // of those are already claimed by the cull passes, and because shadow and main cull must agree
    // on the policy even though they run with different viewports.
    public float LodBaseThreshold;
    // The same policy for the sun shadow cascades, pre-multiplied on the CPU. A shadow only needs
    // enough geometry for its silhouette, so this is normally the larger of the two -- a larger
    // threshold means a proxy has to project bigger before it earns full detail.
    public float LodShadowThreshold;
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

    public uint visible;        // RenderParticipation bit mask
    public uint excludeFromAS;  // 1 for non-first sections of multi-section models: only
                                // section 0 becomes a TLAS instance (its BLAS covers the model)
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
    // Populated LOD levels for this section, always >= 1. Sections that never got simplified
    // report 1, which makes every LOD query resolve back to level 0.
    public uint lodCount;

    // Discrete levels of detail, embedded rather than kept in a side buffer: GPUScene already sits
    // exactly at the 128-byte push-constant floor and has no address slot left, and both the cull
    // and vertex stages load ModelData anyway, so a level costs no extra fetch.
    //
    // Simplified levels only ever add index data -- meshopt_simplify emits indices into the
    // existing vertex range, and the flat-shaded case that needs fresh normals appends its
    // vertices to the same model -- so vertexOffset never varies by level. Entry 0 mirrors
    // indexOffset/indexCount above so a shader can index by level without branching.
    public uint lodIndexOffset[MAX_MODEL_LOD_COUNT];
    public uint lodIndexCount[MAX_MODEL_LOD_COUNT];
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
    public uint SkyVisibilityPZNZPYNY; // 8-bit sky visibility per face: +Z / -Z / +Y / -Y
    public uint SkyVisibilityPXNXS0S1; // +X / -X + two spare lanes
};

// 8 bytes per voxel. matId stays a full uint; distanceToSolid stores 8 packed 4-bit channels:
// x=min distance, y=inside, z/w=+Z/-Z, upper nibbles=+X/-X/+Y/-Y.
public struct ALIGN_8 VoxelData
{
    public uint matId; // 0 is void, that is clean default value
    public uint distanceToSolid; // 0.0 - 1.0 quantized to 4 bits per channel
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

// Tracing extension resource table reached via GPUScene.ReservedAddress0. PathTracing may
// expose SHARC and ReSTIR together; SoftwareTracing exposes ReSTIR only. Per-feature
// availability is judged by its field being non-zero, never by the table pointer itself.
public struct ALIGN_16 FTracingExtras
{
    public uint64_t HashEntries;          // SHARC hash keys
    public uint64_t LockBuffer;           // SHARC bucket locks
    public uint64_t Accumulation;         // SHARC accumulation entries
    public uint64_t Resolved;             // SHARC resolved entries
    public uint64_t Parameters;           // SharcRuntimeParameters
    public uint64_t RestirReservoirPing;  // FRestirReservoir[], buffer A
    public uint64_t RestirReservoirPong;  // FRestirReservoir[], buffer B
    public uint64_t RestirParameters;     // FRestirRuntimeParameters
};

// Screen-space ReSTIR DI reservoir, 16 bytes per pixel, double buffered (ping/pong).
// Stores the sample's generation parameters (light index + light-surface uv) instead of a
// world-space point so reuse follows animated lights and needs no Jacobian.
public struct ALIGN_16 FRestirReservoir
{
    public uint LightData;      // low 24 bits: light index; high 8 bits: sample type (0 = empty, 1 = light)
    public uint PackedUV;       // sample uv on the light quad, 2x unorm16
    public float WeightW;       // unbiased contribution weight W
    public uint PackedMTarget;  // low 16 bits: clamped M; high 16 bits: fp16 target luminance
};

public struct ALIGN_16 FRestirRuntimeParameters
{
    public uint FrameIndex;       // parity selects the current write buffer (ping on even)
    public uint DebugMode;
    public uint Flags;            // bit0 temporal enable, bit1 spatial enable, bit2 temporal history valid this frame
    public uint LightsGeneration; // bumped when the light set is re-ordered; mismatched history resets M

    public uint ReservoirWidth;
    public uint ReservoirHeight;
    public uint InitialCandidates;
    public uint TemporalMClamp;

    public float SpatialRadius;
    public uint SpatialSamples;
    public uint Pad0;
    public uint Pad1;
};

public struct ALIGN_16 SphericalHarmonics
{
    // 3 bands (9 coefficients per color channel)
    public float coefficients[3][9];
    public float padding;
};

public static const uint LightTypeArea = 0;
public static const uint LightTypePoint = 1;

public struct ALIGN_16 LightObject
{
    // Area: p0/p1/p3 are three quad corners and normal_area.w is its area.
    // Point: p0.xyz is the position and p0.w is the optional visible proxy radius.
    float4 p0;
    float4 p1;
    float4 p3;
    float4 normal_area;

    uint lightMatIdx;
    uint reserved1;
    uint reserved2;
    uint lightType;
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
    public float Reserved0;
    public float Reserved1;
    // out
    public float4 HitPoint;
    public float4 Normal;
    public float T;
    public uint InstanceId;
    public uint MaterialId;
    public uint Hit;
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
    public float Reserved2;
    public float Reserved3;
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
    // constant (see ZeroBindCustomPushConstantPipeline). CustomData0 == active RenderView RT
    // bank base, readable via gpuScene.CustomData0 in *all* passes regardless of which pipeline.
    uint32_t SwapChainIndex;
    uint32_t CustomData0;
    uint32_t CustomData1;
    uint32_t CustomData2;

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
    public uint CustomData0;
    public uint CustomData1;
    public uint CustomData2;

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
    public property FTracingExtras* TracingExtras
    {
        get { return (FTracingExtras*)ReservedAddress0; }
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
    public property LightObject* Lights
    {
        get { return (LightObject*)SoftMeshShader.Lights; }
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
    public uint32_t CustomData0;
    public uint32_t CustomData1;
    public uint32_t CustomData2;

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

    /* Global Vertex Buffer */
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

    // Raw pass-local resource address, and the only one of its kind: GPUScene sits exactly at the
    // 128-byte push-constant floor, so there is no second slot to hand out. TracingExtras below is
    // the ReSTIR interpretation of this word; the shading scheduler interprets it as its tile-list
    // buffer. Exactly one subsystem may claim it per pass - the interpretations are raw pointer
    // casts, so a second claimant does not fail loudly, it reads the other one's data as its own
    // and takes the device down. Today ReSTIR belongs to the hardware path tracer and the scheduler
    // to the surface renderers, and those sets do not overlap.
    public property uint64_t ReservedAddress0
    {
        get { return Reserved_AmbientBase_Address.x; }
    }

    public property FTracingExtras* TracingExtras
    {
        get { return (FTracingExtras*)Reserved_AmbientBase_Address.x; }
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
    public property LightObject* Lights
    {
        get { return (LightObject*)SoftMeshShader.Lights; }
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
