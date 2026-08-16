import Shader.Bindless;
import Shader.Types;
import Shader.GpuScene;
import Shader.Tonemap;
import Shader.ShadingScheduler;

#include "common/GTAOUpsample.slang"

static const uint OBJECT_ID_FLAG_VALID = 0x80000000u;
static const uint OBJECT_ID_FLAG_SELECTED = 0x40000000u;
static const uint OBJECT_ID_FLAG_HOVERED = 0x20000000u;
static const uint OBJECT_ID_FLAG_LOCKED = 0x10000000u;
static const uint OBJECT_ID_FLAG_DANGER = 0x08000000u;
static const float DIAGONAL_EDGE_WEIGHT = 0.70710678;

bool HasObjectFlag(uint objectId, uint flag)
{
    return ((objectId & OBJECT_ID_FLAG_VALID) != 0) && ((objectId & flag) != 0);
}

uint SafeLoadObjectId(RWTexture2D<uint> objectImage, int2 ipos, int2 imageMax)
{
    return objectImage[clamp(ipos, int2(0, 0), imageMax)];
}

float ComputeEdgeStrengthTwoSided(RWTexture2D<uint> objectImage, int2 ipos, int2 imageMax, uint flag)
{
    bool centerFlagged = HasObjectFlag(SafeLoadObjectId(objectImage, ipos, imageMax), flag);

    float edgeSum = 0.0;
    float weightSum = 0.0;

    bool x0 = HasObjectFlag(SafeLoadObjectId(objectImage, ipos + int2(1, 0), imageMax), flag);
    bool x1 = HasObjectFlag(SafeLoadObjectId(objectImage, ipos + int2(-1, 0), imageMax), flag);
    bool y0 = HasObjectFlag(SafeLoadObjectId(objectImage, ipos + int2(0, 1), imageMax), flag);
    bool y1 = HasObjectFlag(SafeLoadObjectId(objectImage, ipos + int2(0, -1), imageMax), flag);
    bool d0 = HasObjectFlag(SafeLoadObjectId(objectImage, ipos + int2(1, 1), imageMax), flag);
    bool d1 = HasObjectFlag(SafeLoadObjectId(objectImage, ipos + int2(-1, -1), imageMax), flag);
    bool d2 = HasObjectFlag(SafeLoadObjectId(objectImage, ipos + int2(-1, 1), imageMax), flag);
    bool d3 = HasObjectFlag(SafeLoadObjectId(objectImage, ipos + int2(1, -1), imageMax), flag);

    edgeSum += (x0 != centerFlagged) ? 1.0 : 0.0;
    edgeSum += (x1 != centerFlagged) ? 1.0 : 0.0;
    edgeSum += (y0 != centerFlagged) ? 1.0 : 0.0;
    edgeSum += (y1 != centerFlagged) ? 1.0 : 0.0;
    weightSum += 4.0;

    edgeSum += ((d0 != centerFlagged) ? 1.0 : 0.0) * DIAGONAL_EDGE_WEIGHT;
    edgeSum += ((d1 != centerFlagged) ? 1.0 : 0.0) * DIAGONAL_EDGE_WEIGHT;
    edgeSum += ((d2 != centerFlagged) ? 1.0 : 0.0) * DIAGONAL_EDGE_WEIGHT;
    edgeSum += ((d3 != centerFlagged) ? 1.0 : 0.0) * DIAGONAL_EDGE_WEIGHT;
    weightSum += DIAGONAL_EDGE_WEIGHT * 4.0;

    float rawEdge = edgeSum / max(1e-5, weightSum);
    return smoothstep(0.1, 0.4, rawEdge);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    const UniformBufferObject camera = Bindless.GetGpuscene().Camera[0];
    let OutImage = Bindless.GetViewStorageTexture<float4>(Bindless.RT_SCENE_COLOR);
    let ObjectIdImage = Bindless.GetViewStorageTexture<uint>(Bindless.RT_OBJECTID_0);

    const uint2 fullSize = Bindless.GetViewStorageTextureDimensions<float4>(Bindless.RT_SCENE_COLOR);
    const int2 pixel = int2(DTid.xy);
    if (DTid.x >= fullSize.x || DTid.y >= fullSize.y)
    {
        return;
    }


    // Sparse checkerboard lighting: the parity Core Shading did not write is left untouched all the
    // way to Native TAAU, which reconstructs it from history instead of from a neighbour copy.
    if (Scheduler.ShouldSkipPixel(uint2(pixel), camera.CheckerboardSparseLighting, true,
                                  camera.TotalFrames))
    {
        return;
    }

    const int2 imageMax = int2(fullSize) - int2(1, 1);
    const float4 directSpecularOrEmissive = Bindless.GetViewStorageTexture<float4>(Bindless.RT_SINGLE_DIFFUSE)[pixel];
    const float4 ambient = Bindless.GetViewStorageTexture<float4>(Bindless.RT_AMBIENT)[pixel];
    const float depth = Bindless.GetViewStorageTexture<float>(Bindless.RT_PREV_DEPTHBUFFER)[pixel];
    const float3 worldNormal = Bindless.GetViewStorageTexture<float4>(Bindless.RT_NORMAL)[pixel].xyz;
    const bool surface = GTAOIsSurfaceDepth(depth) && dot(worldNormal, worldNormal) >= 0.25f;

    // GTAO runs before Core Shading, so the shading kernel already upsampled the occlusion and
    // folded it into RT_AMBIENT. Applying it again here would square it.
    float3 total = directSpecularOrEmissive.rgb + ambient.rgb;
    if (camera.GTAODebugMode == 1u)
    {
        // Debug view of the raw visibility, so it upsamples regardless of who applied it.
        const float debugExposure = 40000.0f / max(camera.PaperWhiteNit, 1.0f);
        const float ao = camera.GTAOEnable && surface
            ? GTAOUpsample(camera, pixel, fullSize, depth, normalize(worldNormal))
            : 1.0f;
        total = surface ? float3(ao * debugExposure) : float3(0, 0, 0);
    }
    else if (camera.GTAODebugMode == 2u)
    {
        // The sky diffuse channel; it already carries the occlusion Core Shading applied.
        total = ambient.rgb;
    }
    const float exposure = max(camera.PaperWhiteNit / 40000.0f, 1.0e-6f);
    total = max(total, float3(0.0f)) * exposure;

    const float dangerEdge = ComputeEdgeStrengthTwoSided(ObjectIdImage, pixel, imageMax, OBJECT_ID_FLAG_DANGER);
    const float lockedEdge = ComputeEdgeStrengthTwoSided(ObjectIdImage, pixel, imageMax, OBJECT_ID_FLAG_LOCKED);
    const float selectedEdge = ComputeEdgeStrengthTwoSided(ObjectIdImage, pixel, imageMax, OBJECT_ID_FLAG_SELECTED);
    const float hoveredEdge = ComputeEdgeStrengthTwoSided(ObjectIdImage, pixel, imageMax, OBJECT_ID_FLAG_HOVERED);

    if (dangerEdge > 0)
    {
        total = lerp(total, float3(255, 45, 35) * exposure, saturate(dangerEdge * 0.90));
    }
    else if (lockedEdge > 0)
    {
        total = lerp(total, float3(35, 145, 255) * exposure, saturate(lockedEdge * 0.75));
    }
    else if (selectedEdge > 0)
    {
        total = lerp(total, float3(150, 100, 0) * exposure, saturate(selectedEdge * 0.80));
    }
    else if (hoveredEdge > 0)
    {
        total = lerp(total, float3(80, 220, 120) * exposure, saturate(hoveredEdge * 0.75));
    }

    const bool noSkyBackground = !camera.HasSky && !surface;
    if (noSkyBackground)
    {
        total = NoSkyClearSceneColor(camera.HDR, camera.PaperWhiteNit);
    }
    OutImage[pixel] = float4(total, 1.0);
}
