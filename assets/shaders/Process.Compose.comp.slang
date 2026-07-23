import Shader.Bindless;
import Shader.Types;
import Shader.GpuScene;

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

    const bool progressiveRender = Bindless.GetGpuscene().CustomData1 != 0u;
    let objectId0 = Bindless.GetViewStorageTexture<uint>(Bindless.RT_OBJECTID_0);
    let objectId1 = Bindless.GetViewStorageTexture<uint>(Bindless.RT_OBJECTID_1);
    let sceneColor = Bindless.GetViewStorageTexture<float4>(Bindless.RT_SCENE_COLOR);

    const int2 pixel = int2(dispatchThreadId.xy) + int2(camera.ViewportRect.xy);
    const float exposure = max(camera.PaperWhiteNit / 40000.0, 1.0e-6);
    float3 total = max(LoadComposedRadiance(pixel, progressiveRender), 0.0) * exposure;

    // Kill isolated Monte-Carlo energy before a temporal upscaler can spread it spatially
    // or lock it into history. CustomData2 is zero when disabled, otherwise it stores k sigma.
    const float fireflySigma = asfloat(Bindless.GetGpuscene().CustomData2);
    if (!progressiveRender && fireflySigma > 0.0)
    {
        const int2 viewportMin = int2(camera.ViewportRect.xy);
        const int2 viewportMax = min(
            viewportMin + int2(camera.ViewportRect.zw) - int2(1, 1),
            int2(outputSize) - int2(1, 1));
        float neighborMean = 0.0;
        float neighborSquareMean = 0.0;
        uint neighborCount = 0;
        [unroll]
        for (int y = -1; y <= 1; ++y)
        {
            [unroll]
            for (int x = -1; x <= 1; ++x)
            {
                if (x == 0 && y == 0)
                {
                    continue;
                }
                const int2 samplePixel = clamp(pixel + int2(x, y), viewportMin, viewportMax);
                const float sampleLuma = Luminance(
                    LoadComposedRadiance(samplePixel, false) * exposure);
                neighborMean += sampleLuma;
                neighborSquareMean += sampleLuma * sampleLuma;
                ++neighborCount;
            }
        }
        const float reciprocalCount = 1.0 / max(float(neighborCount), 1.0);
        neighborMean *= reciprocalCount;
        neighborSquareMean *= reciprocalCount;
        const float neighborDeviation = sqrt(max(
            neighborSquareMean - neighborMean * neighborMean, 0.0));
        const float fireflyCeiling =
            neighborMean + max(0.01, fireflySigma * neighborDeviation);
        const float centerLuma = Luminance(total);
        if (centerLuma > fireflyCeiling)
        {
            total *= fireflyCeiling / max(centerLuma, 1.0e-5);
        }
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

    sceneColor[pixel] = float4(total, noSkyBackground ? 0.0 : 1.0);
}
