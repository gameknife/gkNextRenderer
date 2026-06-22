import Common;
#include "common/PreProcessor.slang"

// GPU voxel sky-visibility bake (voxel-skyvisibility design §5.3).
//
// For each near-surface air voxel of a cascade, traces N cosine-weighted rays over the upper
// hemisphere against the voxel distance field (FHiVoxelDDARayTracer::TraceOcclusionDDA) and stores
// the unoccluded fraction (0-255) into VoxelData.age's high byte. The low 24 bits keep the temporal
// counter so the result accumulates across frames and resets to 0 when the CPU re-voxelizes.
//
// Dispatched per cascade over the dense voxel grid, temporally sliced via custom_data_0:
//   custom_data_0 = linear voxel index base for this frame's slice (within the cascade)
//   custom_data_1 = cascade index

static const uint kSkyVisSurfaceBand = 8u;     // skip-byte threshold: only bake voxels near a surface
static const float kSkyVisHistoryLength = 16.0f; // temporal accumulation window (matches cube baker)

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    UniformBufferObject camera = Bindless.GetGpuscene().Camera[0];
    VoxelData* Voxels = Bindless.GetGpuscene().Voxels;

    uint cascadeIndex = Bindless.GetGpuscene().custom_data_1;
    uint localIdx = DTid.x + Bindless.GetGpuscene().custom_data_0;

    const uint perCascade = uint(CUBE_SIZE_XY * CUBE_SIZE_XY * CUBE_SIZE_Z);
    if (localIdx >= perCascade)
    {
        return;
    }

    uint voxelIdx = GetCascadeBaseOffset(cascadeIndex) + localIdx;

    // Decode probe grid position from the dense linear index (idx = y*XY*XY + z*XY + x).
    int x = int(localIdx % uint(CUBE_SIZE_XY));
    int z = int((localIdx / uint(CUBE_SIZE_XY)) % uint(CUBE_SIZE_XY));
    int y = int(localIdx / uint(CUBE_SIZE_XY * CUBE_SIZE_XY));
    int3 probePos = int3(x, y, z);

    // Only bake near-surface air voxels: solid voxels do not shade, and open air keeps the default
    // (fully visible) so it costs nothing and reads back as skyVis=1 via the sampler fallback.
    uint4 dgg = UnpackBytes(Voxels[voxelIdx].distanceToSolid_gg_z01);
    bool isAir = (Voxels[voxelIdx].matId == 0u) && (float(dgg.y) / 255.0f >= 0.01f);
    bool nearSurface = isAir && (dgg.x < kSkyVisSurfaceBand);
    if (!nearSurface)
    {
        return;
    }

    float cubeUnit = GetAmbientCubeUnitAtCascade(cascadeIndex);
    float3 origin = float3(probePos) * cubeUnit + GetAmbientCubeOffsetAtCascade(cascadeIndex);

    uint iterate = GetVoxelAgeCounter(Voxels[voxelIdx]);

    uint rayCount = max(camera.SkyVisRayCount, 1u);
    float maxDist = max(camera.SkyVisMaxDistance, cubeUnit);

    uint4 randomSeed = InitRandomSeed(uint(probePos.x + probePos.y * 17), uint(probePos.z + iterate * 31),
                                      camera.TotalFrames);

    FHiVoxelDDARayTracer tracer;
    uint unoccluded = 0u;
    for (uint i = 0; i < rayCount; ++i)
    {
        // Cosine-weighted upper hemisphere (+Y is sky-up).
        float3 dir = AlignWithNormal(RandomInHemiSphere1(randomSeed), float3(0, 1, 0));
        if (!tracer.TraceOcclusionDDA(origin, dir, maxDist))
        {
            unoccluded++;
        }
    }
    float skyVisFrac = float(unoccluded) / float(rayCount);

    // Temporal accumulation, same weighting as the ambient cube baker (kTraceHistoryLength frames).
    float accumulatedFrames = min(float(iterate) + 1.0f, kSkyVisHistoryLength);
    float currWeight = 1.0f / accumulatedFrames;
    float prev = float(GetVoxelSkyVis(Voxels[voxelIdx]));
    // Reserve byte 0 as "unbaked": clamp the stored value to [1,255] so the runtime sampler can tell
    // a freshly voxelized (skyVis==0) voxel apart from a baked fully-occluded one (skyVis==1 ~= black).
    uint next = uint(clamp(lerp(prev, 255.0f * skyVisFrac, currWeight), 1.0f, 255.0f));

    SetVoxelSkyVis(Voxels[voxelIdx], next);
    SetVoxelAgeCounter(Voxels[voxelIdx], iterate + 1);
}
