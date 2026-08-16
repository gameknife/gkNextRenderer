import Shader.Bindless;
import Shader.Types;
import Shader.GpuScene;
import Shader.Tonemap;
import Shader.ShadingScheduler;

// Direct sample compose. Realtime rendering reads the current diffuse/specular sample;
// explicit offline progressive rendering reads the running-average linear scene color instead.
// The resulting scene color is pre-exposed linear HDR with display white near 1.0. Display
// encoding is performed only by the final output pass after temporal upscaling.

static const uint OBJECT_ID_FLAG_VALID = 0x80000000u;
static const uint OBJECT_ID_FLAG_SELECTED = 0x40000000u;
static const uint OBJECT_ID_FLAG_HOVERED = 0x20000000u;
static const uint OBJECT_ID_FLAG_LOCKED = 0x10000000u;
static const uint OBJECT_ID_FLAG_DANGER = 0x08000000u;
static const float DIAGONAL_EDGE_WEIGHT = 0.70710678;

float Luminance(float3 color)
{
    return dot(max(color, float3(0.0)), float3(0.2126, 0.7152, 0.0722));
}

bool HasObjectFlag(uint objectId, uint flag)
{
    return ((objectId & OBJECT_ID_FLAG_VALID) != 0) && ((objectId & flag) != 0);
}

float3 LoadComposedRadiance(int2 pixel, bool progressiveRender)
{
    const UniformBufferObject camera = Bindless.GetGpuscene().Camera[0];
    if (progressiveRender)
    {
        return Bindless.GetViewStorageTexture<float4>(
            Bindless.RT_PROGRESSIVE_SCENE_COLOR)[pixel].rgb;
    }

    const float3 diffuse = Bindless.GetViewStorageTexture<float4>(
        Bindless.RT_SINGLE_DIFFUSE)[pixel].rgb;
    const float3 specular = Bindless.GetViewStorageTexture<float4>(
        Bindless.RT_SINGLE_SPECULAR)[pixel].rgb;
    const float3 albedo = Bindless.GetViewStorageTexture<float4>(
        Bindless.RT_ALBEDO)[pixel].rgb;
    return camera.DebugDraw_Lighting
        ? diffuse * float3(0.5, 0.5, 0.5) + specular
        : diffuse * albedo + specular;
}

float ComputeEdgeStrengthTwoSided(RWTexture2D<uint> objectImage, int2 pixel, uint flag)
{
    const bool centerFlagged = HasObjectFlag(objectImage[pixel], flag);

    float edgeSum = 0.0;
    float weightSum = 0.0;
    edgeSum += (HasObjectFlag(objectImage[pixel + int2(1, 0)], flag) != centerFlagged) ? 1.0 : 0.0;
    edgeSum += (HasObjectFlag(objectImage[pixel + int2(-1, 0)], flag) != centerFlagged) ? 1.0 : 0.0;
    edgeSum += (HasObjectFlag(objectImage[pixel + int2(0, 1)], flag) != centerFlagged) ? 1.0 : 0.0;
    edgeSum += (HasObjectFlag(objectImage[pixel + int2(0, -1)], flag) != centerFlagged) ? 1.0 : 0.0;
    weightSum += 4.0;

    edgeSum += ((HasObjectFlag(objectImage[pixel + int2(1, 1)], flag) != centerFlagged) ? 1.0 : 0.0) * DIAGONAL_EDGE_WEIGHT;
    edgeSum += ((HasObjectFlag(objectImage[pixel + int2(-1, -1)], flag) != centerFlagged) ? 1.0 : 0.0) * DIAGONAL_EDGE_WEIGHT;
    edgeSum += ((HasObjectFlag(objectImage[pixel + int2(-1, 1)], flag) != centerFlagged) ? 1.0 : 0.0) * DIAGONAL_EDGE_WEIGHT;
    edgeSum += ((HasObjectFlag(objectImage[pixel + int2(1, -1)], flag) != centerFlagged) ? 1.0 : 0.0) * DIAGONAL_EDGE_WEIGHT;
    weightSum += DIAGONAL_EDGE_WEIGHT * 4.0;

    return smoothstep(0.1, 0.4, edgeSum / max(1e-5, weightSum));
}

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const UniformBufferObject camera = Bindless.GetGpuscene().Camera[0];
    const uint2 outputSize = Bindless.GetViewStorageTextureDimensions<float4>(Bindless.RT_SCENE_COLOR);
    if (any(dispatchThreadId.xy >= outputSize))
    {
        return;
    }

    // Sparse checkerboard lighting: the parity Core Shading did not write is left untouched all the
    // way to Native TAAU, which reconstructs it from history instead of from a neighbour copy.
    if (Scheduler.ShouldSkipPixel(dispatchThreadId.xy, camera.CheckerboardSparseLighting, true,
                                  camera.TotalFrames))
    {
        return;
    }

    const bool progressiveRender = Bindless.GetGpuscene().CustomData1 != 0u;
    let objectId0 = Bindless.GetViewStorageTexture<uint>(Bindless.RT_OBJECTID_0);
    let objectId1 = Bindless.GetViewStorageTexture<uint>(Bindless.RT_OBJECTID_1);
    let sceneColor = Bindless.GetViewStorageTexture<float4>(Bindless.RT_SCENE_COLOR);

    const int2 pixel = int2(dispatchThreadId.xy) + int2(camera.ViewportRect.xy);
    const float exposure = max(camera.PaperWhiteNit / 40000.0, 1.0e-6);
    float3 total = max(LoadComposedRadiance(pixel, progressiveRender), 0.0) * exposure;

    const bool noSkyBackground =
        !camera.HasSky && !HasObjectFlag(objectId0[pixel], OBJECT_ID_FLAG_VALID);
    if (noSkyBackground)
    {
        total = NoSkyClearSceneColor(camera.HDR, camera.PaperWhiteNit);
    }

    const float selectedEdge = saturate(
        ComputeEdgeStrengthTwoSided(objectId0, pixel, OBJECT_ID_FLAG_SELECTED) * 0.80 +
        ComputeEdgeStrengthTwoSided(objectId1, pixel, OBJECT_ID_FLAG_SELECTED) * 0.20);
    const float hoveredEdge = saturate(
        ComputeEdgeStrengthTwoSided(objectId0, pixel, OBJECT_ID_FLAG_HOVERED) * 0.75 +
        ComputeEdgeStrengthTwoSided(objectId1, pixel, OBJECT_ID_FLAG_HOVERED) * 0.25);
    const float lockedEdge = saturate(
        ComputeEdgeStrengthTwoSided(objectId0, pixel, OBJECT_ID_FLAG_LOCKED) * 0.75 +
        ComputeEdgeStrengthTwoSided(objectId1, pixel, OBJECT_ID_FLAG_LOCKED) * 0.25);
    const float dangerEdge = saturate(
        ComputeEdgeStrengthTwoSided(objectId0, pixel, OBJECT_ID_FLAG_DANGER) * 0.90 +
        ComputeEdgeStrengthTwoSided(objectId1, pixel, OBJECT_ID_FLAG_DANGER) * 0.25);

    if (dangerEdge > 0.0)
    {
        total = lerp(total, float3(255, 45, 35) * exposure, dangerEdge);
    }
    else if (lockedEdge > 0.0)
    {
        total = lerp(total, float3(35, 145, 255) * exposure, lockedEdge);
    }
    else if (selectedEdge > 0.0)
    {
        total = lerp(total, float3(150, 100, 0) * exposure, selectedEdge);
    }
    else if (hoveredEdge > 0.0)
    {
        total = lerp(total, float3(80, 220, 120) * exposure, hoveredEdge);
    }

    sceneColor[pixel] = float4(total, 1.0);
}
