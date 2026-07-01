import Shader.Bindless;
import Shader.Types;
import Shader.GpuScene;
import Shader.Tonemap;

static const uint OBJECT_ID_FLAG_VALID = 0x80000000u;
static const uint OBJECT_ID_FLAG_SELECTED = 0x40000000u;
static const uint OBJECT_ID_FLAG_HOVERED = 0x20000000u;
static const uint OBJECT_ID_FLAG_LOCKED = 0x10000000u;
static const uint OBJECT_ID_FLAG_DANGER = 0x08000000u;
static const float DIAGONAL_EDGE_WEIGHT = 0.70710678;

bool IsSurfaceDepth(float depth)
{
    return depth >= 0.0f && depth <= 1.0f;
}

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

float3 ReconstructViewPosition(UniformBufferObject camera, int2 pixel, uint2 size, float depth)
{
    const float2 ndc = ((float2(pixel) + 0.5f) / float2(size)) * 2.0f - 1.0f;
    const float4 view = mul(camera.ProjectionInverse, float4(ndc, depth, 1.0f));
    const float safeW = abs(view.w) > 1e-6f ? view.w : (view.w >= 0.0f ? 1e-6f : -1e-6f);
    return view.xyz / safeW;
}

float UpsampleAO(UniformBufferObject camera, int2 pixel, uint2 fullSize, float centerDepth, float3 centerNormal)
{
    const uint2 aoSize = Bindless.GetViewStorageTextureDimensions<float>(Bindless.RT_GTAO);
    const int2 aoMax = int2(aoSize) - int2(1, 1);
    const int2 fullMax = int2(fullSize) - int2(1, 1);
    const float3 centerPosition = ReconstructViewPosition(camera, pixel, fullSize, centerDepth);
    const float2 halfPosition = (float2(pixel) + 0.5f) * 0.5f - 0.5f;
    const int2 halfBase = int2(floor(halfPosition));
    float weightedAO = 0.0f;
    float weightSum = 0.0f;

    // A 3x3 joint bilateral footprint is still cheap at full resolution and finds valid
    // geometry when a temporally jittered half-resolution representative landed on sky.
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            const int2 aoPixel = clamp(halfBase + int2(x, y), int2(0, 0), aoMax);
            const int2 representative = min(aoPixel * 2 + int2(1, 1), fullMax);
            const float sampleDepth = Bindless.GetViewStorageTexture<float>(Bindless.RT_PREV_DEPTHBUFFER)[representative];
            const float3 sampleNormal = Bindless.GetViewStorageTexture<float4>(Bindless.RT_NORMAL)[representative].xyz;
            if (!IsSurfaceDepth(sampleDepth) || dot(sampleNormal, sampleNormal) < 0.25f)
            {
                continue;
            }

            const float3 samplePosition = ReconstructViewPosition(camera, representative, fullSize, sampleDepth);
            const float normalWeight = pow(saturate(dot(centerNormal, normalize(sampleNormal))), 32.0f);
            const float depthScale = max(0.02f, abs(centerPosition.z) * 0.02f);
            const float depthWeight = exp2(-abs(samplePosition.z - centerPosition.z) / depthScale);
            const float2 spatialDelta = float2(aoPixel) - halfPosition;
            const float spatialWeight = exp2(-dot(spatialDelta, spatialDelta));
            const float weight = normalWeight * depthWeight * spatialWeight;
            weightedAO += Bindless.GetViewStorageTexture<float>(Bindless.RT_GTAO)[aoPixel] * weight;
            weightSum += weight;
        }
    }

    if (weightSum <= 1e-5f)
    {
        return Bindless.GetViewStorageTexture<float>(Bindless.RT_GTAO)[clamp(pixel / 2, int2(0, 0), aoMax)];
    }
    return weightedAO / weightSum;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    const UniformBufferObject camera = Bindless.GetGpuscene().Camera[0];
    let OutImage = Bindless.GetViewStorageTexture<float4>(Bindless.RT_DENOISED);
    let ObjectIdImage = Bindless.GetViewStorageTexture<uint>(Bindless.RT_OBJEDCTID_0);

    const uint2 fullSize = Bindless.GetViewStorageTextureDimensions<float4>(Bindless.RT_DENOISED);
    const int2 pixel = int2(DTid.xy);
    if (DTid.x >= fullSize.x || DTid.y >= fullSize.y)
    {
        return;
    }

    const int2 imageMax = int2(fullSize) - int2(1, 1);
    const float4 directSpecularOrEmissive = Bindless.GetViewStorageTexture<float4>(Bindless.RT_SINGLE_DIFFUSE)[pixel];
    const float4 ambient = Bindless.GetViewStorageTexture<float4>(Bindless.RT_AMBIENT)[pixel];
    const float depth = Bindless.GetViewStorageTexture<float>(Bindless.RT_PREV_DEPTHBUFFER)[pixel];
    const float3 worldNormal = Bindless.GetViewStorageTexture<float4>(Bindless.RT_NORMAL)[pixel].xyz;
    const bool surface = IsSurfaceDepth(depth) && dot(worldNormal, worldNormal) >= 0.25f;
    float ao = 1.0f;
    if (camera.GTAOEnable && surface)
    {
        ao = UpsampleAO(camera, pixel, fullSize, depth, normalize(worldNormal));
    }

    // Gamma-style master strength: monotone and never hard-clips to pure black (unlike a linear
    // 1 - strength*(1-vis), which zeroes everything below 1-1/strength). This keeps GTAO's gradient
    // visible in deep corners as a soft falloff instead of a dead-black patch.
    const float strength = max(camera.GTAOStrength, 0.0f);
    const float skyOcclusion = pow(saturate(ao), strength);

    float3 total = directSpecularOrEmissive.rgb + ambient.rgb * skyOcclusion;
    if (camera.GTAODebugMode == 1u)
    {
        const float debugExposure = 40000.0f / max(camera.PaperWhiteNit, 1.0f);
        total = surface ? float3(ao * debugExposure) : float3(0, 0, 0);
    }
    else if (camera.GTAODebugMode == 2u)
    {
        total = ambient.rgb;
    }

    const float dangerEdge = ComputeEdgeStrengthTwoSided(ObjectIdImage, pixel, imageMax, OBJECT_ID_FLAG_DANGER);
    const float lockedEdge = ComputeEdgeStrengthTwoSided(ObjectIdImage, pixel, imageMax, OBJECT_ID_FLAG_LOCKED);
    const float selectedEdge = ComputeEdgeStrengthTwoSided(ObjectIdImage, pixel, imageMax, OBJECT_ID_FLAG_SELECTED);
    const float hoveredEdge = ComputeEdgeStrengthTwoSided(ObjectIdImage, pixel, imageMax, OBJECT_ID_FLAG_HOVERED);

    if (dangerEdge > 0)
    {
        total = lerp(total, float3(255, 45, 35), saturate(dangerEdge * 0.90));
    }
    else if (lockedEdge > 0)
    {
        total = lerp(total, float3(35, 145, 255), saturate(lockedEdge * 0.75));
    }
    else if (selectedEdge > 0)
    {
        total = lerp(total, float3(150, 100, 0), saturate(selectedEdge * 0.80));
    }
    else if (hoveredEdge > 0)
    {
        total = lerp(total, float3(80, 220, 120), saturate(hoveredEdge * 0.75));
    }

    if (camera.HDR)
    {
        OutImage[pixel] = float4(EncodeHdrOutput(total, camera.PaperWhiteNit, camera.HDROutputMode), 1.0);
    }
    else
    {
        OutImage[pixel] = float4(GT_Tonemapping(total * camera.PaperWhiteNit / 40000.0), 1.0);
    }
}
