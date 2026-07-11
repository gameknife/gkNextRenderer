import Shader.Bindless;
import Shader.Types;
import Shader.GpuScene;
import Shader.Tonemap;

// Final compose pass. Spatial denoising is done upstream by the variance-guided a-trous
// wavelet (PipelineCommon::AtrousDenoiser); this pass only composes the (already filtered or
// raw) diffuse + specular with albedo, draws selection/hover/lock/danger outlines, and tonemaps
// to the swapchain. The diffuse/specular source slots are routed by the renderer via
// Camera.Denoise*SourceSlot (a-trous output when the denoiser is on, else the accumulation buffers).

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

float ComputeEdgeStrengthTwoSided(RWTexture2D<uint> ObjectImage, int2 ipos, uint flag)
{
    // Draw on both sides of the boundary, used for thicker selected outline.
    bool centerFlagged = HasObjectFlag(ObjectImage[ipos], flag);

    float edgeSum = 0.0;
    float weightSum = 0.0;

    bool x0 = HasObjectFlag(ObjectImage[ipos + int2(1, 0)], flag);
    bool x1 = HasObjectFlag(ObjectImage[ipos + int2(-1, 0)], flag);
    bool y0 = HasObjectFlag(ObjectImage[ipos + int2(0, 1)], flag);
    bool y1 = HasObjectFlag(ObjectImage[ipos + int2(0, -1)], flag);
    bool d0 = HasObjectFlag(ObjectImage[ipos + int2(1, 1)], flag);
    bool d1 = HasObjectFlag(ObjectImage[ipos + int2(-1, -1)], flag);
    bool d2 = HasObjectFlag(ObjectImage[ipos + int2(-1, 1)], flag);
    bool d3 = HasObjectFlag(ObjectImage[ipos + int2(1, -1)], flag);

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
void main(uint3 dispatchThreadId: SV_DispatchThreadID,
                 uint3 groupThreadId: SV_GroupThreadID,
                 uint3 groupId: SV_GroupID,
                 uint groupIndex: SV_GroupIndex)
{
    UniformBufferObject Camera = Bindless.GetGpuscene().Camera[0];

    let OutImage = Bindless.GetViewStorageTexture<float4>(Bindless.RT_DENOISED);
    const uint2 outputSize = Bindless.GetViewStorageTextureDimensions<float4>(Bindless.RT_DENOISED);
    if (any(dispatchThreadId.xy >= outputSize))
    {
        return;
    }
    // Diffuse/specular source is routed by the renderer: the a-trous output when the
    // variance-guided path is active, otherwise the temporal accumulation buffer.
    // Source slots are absolute screen-space slots; resolve into the active view's bank.
    let vb = Bindless.GetGpuscene().custom_data_0;
    let FinalImage = Bindless.GetStorageTexture<float4>(Bindless.ViewRT(vb, int(Camera.DenoiseDiffuseSourceSlot)));
    let FinalImageSpec = Bindless.GetStorageTexture<float4>(Bindless.ViewRT(vb, int(Camera.DenoiseSpecularSourceSlot)));
    let AccumlateAlbedo = Bindless.GetViewStorageTexture<float4>(Bindless.RT_ACCUMLATE_ALBEDO);
    let ObjectId0 = Bindless.GetViewStorageTexture<uint>(Bindless.RT_OBJEDCTID_0);
    let ObjectId1 = Bindless.GetViewStorageTexture<uint>(Bindless.RT_OBJEDCTID_1);

    const int2 globalOffset = int2(Camera.ViewportRect.xy);
    const int2 ipos = int2(dispatchThreadId.xy) + globalOffset;

    float3 Total;
    if (Camera.DebugDraw_Lighting)
    {
        Total = FinalImage[ipos].rgb * float3(0.5, 0.5, 0.5) + FinalImageSpec[ipos].rgb;
    }
    else
    {
        Total = FinalImage[ipos].rgb * AccumlateAlbedo[ipos].rgb + FinalImageSpec[ipos].rgb;
    }

    float selectedEdge = saturate(ComputeEdgeStrengthTwoSided(ObjectId0, ipos, OBJECT_ID_FLAG_SELECTED) * 0.80 +
                                  ComputeEdgeStrengthTwoSided(ObjectId1, ipos, OBJECT_ID_FLAG_SELECTED) * 0.20);
    float hoveredEdge = saturate(ComputeEdgeStrengthTwoSided(ObjectId0, ipos, OBJECT_ID_FLAG_HOVERED) * 0.75 +
                                 ComputeEdgeStrengthTwoSided(ObjectId1, ipos, OBJECT_ID_FLAG_HOVERED) * 0.25);
    float lockedEdge = saturate(ComputeEdgeStrengthTwoSided(ObjectId0, ipos, OBJECT_ID_FLAG_LOCKED) * 0.75 +
                                ComputeEdgeStrengthTwoSided(ObjectId1, ipos, OBJECT_ID_FLAG_LOCKED) * 0.25);
    float dangerEdge = saturate(ComputeEdgeStrengthTwoSided(ObjectId0, ipos, OBJECT_ID_FLAG_DANGER) * 0.90 +
                                ComputeEdgeStrengthTwoSided(ObjectId1, ipos, OBJECT_ID_FLAG_DANGER) * 0.25);

    // Priority: danger > locked > selected > hovered
    if (dangerEdge > 0)
    {
        Total = lerp(Total, float3(255, 45, 35), dangerEdge);
    }
    else if (lockedEdge > 0)
    {
        Total = lerp(Total, float3(35, 145, 255), lockedEdge);
    }
    else if (selectedEdge > 0)
    {
        Total = lerp(Total, float3(150, 100, 0), selectedEdge);
    }
    else if (hoveredEdge > 0)
    {
        Total = lerp(Total, float3(80, 220, 120), hoveredEdge);
    }

    if(Camera.HDR)
    {
        OutImage[ipos] = float4(EncodeHdrOutput(Total, Camera.PaperWhiteNit, Camera.HDROutputMode), 1.0);
    }
    else
    {
        OutImage[ipos] = float4(GT_Tonemapping(Total * Camera.PaperWhiteNit / 40000.0), 1.0);
    }
}
