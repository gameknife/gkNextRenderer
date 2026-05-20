import Common;
import Bindless;

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
    let OutImage = Bindless.GetStorageTexture<float4>(Bindless.RT_DENOISED);
    let LightingImage = Bindless.GetStorageTexture<float4>(Bindless.RT_SINGLE_DIFFUSE);
    let AlbedoImage = Bindless.GetStorageTexture<float4>(Bindless.RT_ALBEDO);
    let ObjectIdImage = Bindless.GetStorageTexture<uint>(Bindless.RT_OBJEDCTID_0);

    uint2 size = Bindless.GetStorageTextureDimensions<float4>(Bindless.RT_DENOISED);
    int2 ipos = int2(DTid.xy);
    if (DTid.x >= size.x || DTid.y >= size.y)
    {
        return;
    }

    int2 imageMax = int2(size) - int2(1, 1);
    UniformBufferObject Camera = Bindless.GetGpuscene().Camera[0];

    if (Camera.DebugDraw_ShadowCascadeCoverage)
    {
        OutImage[ipos] = float4(LightingImage[ipos].rgb, 1.0f);
        return;
    }

    float3 total = LightingImage[ipos].rgb;
    if (!Camera.DebugDraw_Lighting)
    {
        total *= AlbedoImage[ipos].rgb;
    }

    float dangerEdge = ComputeEdgeStrengthTwoSided(ObjectIdImage, ipos, imageMax, OBJECT_ID_FLAG_DANGER);
    float lockedEdge = ComputeEdgeStrengthTwoSided(ObjectIdImage, ipos, imageMax, OBJECT_ID_FLAG_LOCKED);
    float selectedEdge = ComputeEdgeStrengthTwoSided(ObjectIdImage, ipos, imageMax, OBJECT_ID_FLAG_SELECTED);
    float hoveredEdge = ComputeEdgeStrengthTwoSided(ObjectIdImage, ipos, imageMax, OBJECT_ID_FLAG_HOVERED);

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

    if (Camera.HDR)
    {
        total = total / 2000;
        total = GT_Tonemapping(total);
        total = total * 2000;
        float3 st2084 = LinearToST2084UE(total * Camera.PaperWhiteNit / 230.0);
        OutImage[ipos] = float4(st2084, 1.0);
    }
    else
    {
        OutImage[ipos] = float4(GT_Tonemapping(total * Camera.PaperWhiteNit / 40000.0), 1.0);
    }
}
