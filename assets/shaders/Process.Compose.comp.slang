import Shader.Bindless;
import Shader.Types;
import Shader.GpuScene;
import Shader.Tonemap;

// Direct sample compose. Realtime rendering reads the current diffuse/specular sample;
// explicit offline progressive rendering reads the running-average linear scene color instead.
// The resulting scene color is the common input for temporal upscalers and native presentation.

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

    const bool progressiveRender = Bindless.GetGpuscene().CustomData1 != 0u;
    let objectId0 = Bindless.GetViewStorageTexture<uint>(Bindless.RT_OBJECTID_0);
    let objectId1 = Bindless.GetViewStorageTexture<uint>(Bindless.RT_OBJECTID_1);
    let sceneColor = Bindless.GetViewStorageTexture<float4>(Bindless.RT_SCENE_COLOR);

    const int2 pixel = int2(dispatchThreadId.xy) + int2(camera.ViewportRect.xy);
    float3 total;
    if (progressiveRender)
    {
        let progressiveSceneColor = Bindless.GetViewStorageTexture<float4>(Bindless.RT_PROGRESSIVE_SCENE_COLOR);
        total = progressiveSceneColor[pixel].rgb;
    }
    else
    {
        let singleDiffuse = Bindless.GetViewStorageTexture<float4>(Bindless.RT_SINGLE_DIFFUSE);
        let singleSpecular = Bindless.GetViewStorageTexture<float4>(Bindless.RT_SINGLE_SPECULAR);
        let albedo = Bindless.GetViewStorageTexture<float4>(Bindless.RT_ALBEDO);
        total = camera.DebugDraw_Lighting
            ? singleDiffuse[pixel].rgb * float3(0.5, 0.5, 0.5) + singleSpecular[pixel].rgb
            : singleDiffuse[pixel].rgb * albedo[pixel].rgb + singleSpecular[pixel].rgb;
    }

    const bool noSkyBackground =
        !camera.HasSky && !HasObjectFlag(objectId0[pixel], OBJECT_ID_FLAG_VALID);

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
        total = lerp(total, float3(255, 45, 35), dangerEdge);
    }
    else if (lockedEdge > 0.0)
    {
        total = lerp(total, float3(35, 145, 255), lockedEdge);
    }
    else if (selectedEdge > 0.0)
    {
        total = lerp(total, float3(150, 100, 0), selectedEdge);
    }
    else if (hoveredEdge > 0.0)
    {
        total = lerp(total, float3(80, 220, 120), hoveredEdge);
    }

    if (camera.HDR)
    {
        const float3 outputColor = noSkyBackground
            ? EncodeNoSkyBackground(true, camera.PaperWhiteNit, camera.HDROutputMode)
            : EncodeHdrOutput(total, camera.PaperWhiteNit, camera.HDROutputMode);
        sceneColor[pixel] = float4(outputColor, 1.0);
    }
    else
    {
        const float3 outputColor = noSkyBackground
            ? EncodeNoSkyBackground(false, camera.PaperWhiteNit, camera.HDROutputMode)
            : GT_Tonemapping(total * camera.PaperWhiteNit / 40000.0);
        sceneColor[pixel] = float4(outputColor, 1.0);
    }
}
