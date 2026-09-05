// Compatibility soft-mesh raster vertex stage.
//
// The full soft-mesh path follows device addresses from GPUScene to locate the expanded primitive
// stream. This version reaches every buffer through one fixed descriptor set instead: binding 5
// holds uint2(proxyIndex, indexBufferOffset) records written by Task.CompatibilitySoftMeshExpand.
// That preserves one indirect draw without Shader.Bindless or bufferDeviceAddress.
//
// The record's index offset is already resolved to the LOD the cull pass chose, so this stage is
// LOD-agnostic and reads ModelData only for vertexOffset, which is shared by every level.
//
// This module also intentionally avoids 16-bit shader types. GPUVertex is read as raw uint words
// and decoded with integer math, so the target only needs ordinary 32-bit storage-buffer support.
import Shader.Types;

[[vk::binding(0, 0)]] StructuredBuffer<NodeProxy> Nodes;
[[vk::binding(1, 0)]] StructuredBuffer<uint> VertexWords;
[[vk::binding(2, 0)]] StructuredBuffer<uint> Indices;
[[vk::binding(3, 0)]] StructuredBuffer<ModelData> Offsets;
[[vk::binding(5, 0)]] StructuredBuffer<uint2> ExpandedPrimitives;

struct CompatibilityDrawPushConstants
{
    float4x4 ViewProjection;
};
[[vk::push_constant]] CompatibilityDrawPushConstants pushConstants;

// GPUVertex is three half4s = 24 bytes = 6 uints (Assets::GPUVertex in Vertex.hpp is 12 uint16s).
static const uint kVertexWordCount = 6;

// IEEE binary16 -> binary32 with integer math only. f16tof32 would be shorter but Slang lowers it
// to a bitcast through `half`, which pulls in the Float16 capability.
float HalfBitsToFloat(uint h)
{
    const uint sign = (h & 0x8000u) << 16u;
    const uint exponent = (h >> 10u) & 0x1Fu;
    const uint mantissa = h & 0x3FFu;
    if (exponent == 0u)
    {
        const float magnitude = float(mantissa) * 5.9604644775390625e-8f;
        return (sign != 0u) ? -magnitude : magnitude;
    }
    if (exponent == 31u)
    {
        return asfloat(sign | 0x7F800000u | (mantissa << 13u)); // inf / NaN
    }
    return asfloat(sign | ((exponent + 112u) << 23u) | (mantissa << 13u));
}

float2 UnpackHalf2(uint packed)
{
    return float2(HalfBitsToFloat(packed & 0xFFFFu), HalfBitsToFloat(packed >> 16u));
}

// Mirrors FetchMaterialId in common/ConstFunc.slang. Spelled out rather than dynamically indexed so
// the constrained MSL backend has nothing to lower into indirect addressing.
uint FetchNodeMaterialId(in NodeProxy node, uint slot)
{
    switch (slot)
    {
    case 0:  return node.matId[0];
    case 1:  return node.matId[1];
    case 2:  return node.matId[2];
    case 3:  return node.matId[3];
    case 4:  return node.matId[4];
    case 5:  return node.matId[5];
    case 6:  return node.matId[6];
    case 7:  return node.matId[7];
    case 8:  return node.matId[8];
    case 9:  return node.matId[9];
    case 10: return node.matId[10];
    case 11: return node.matId[11];
    case 12: return node.matId[12];
    case 13: return node.matId[13];
    case 14: return node.matId[14];
    default: return node.matId[15];
    }
}

struct VertexOutput
{
    float4 position : SV_Position;
    float3 worldNormal : WORLD_NORMAL;
    nointerpolation uint materialId : MATERIAL_ID;
};

[shader("vertex")]
VertexOutput main(uint vertexIndex: SV_VertexID)
{
    VertexOutput output;

    const uint2 primitive = ExpandedPrimitives[vertexIndex / 3u];
    const uint corner = vertexIndex % 3u;
    const NodeProxy proxy = Nodes[primitive.x];
    const ModelData model = Offsets[proxy.modelId];
    const uint localVertex = Indices[primitive.y + corner];
    const uint vertexWord = (model.vertexOffset + localVertex) * kVertexWordCount;

    const float2 positionXY = UnpackHalf2(VertexWords[vertexWord + 0]);
    const float2 positionZ_TexU = UnpackHalf2(VertexWords[vertexWord + 1]);
    const float3 localPosition = float3(positionXY, positionZ_TexU.x);

    const float2 normalXY = UnpackHalf2(VertexWords[vertexWord + 2]);
    const float2 normalZ_TexV = UnpackHalf2(VertexWords[vertexWord + 3]);
    const float3 localNormal = float3(normalXY, normalZ_TexV.x);

    // Tangent.w packs the per-vertex material slot in its low byte (see UnpackVertex).
    const uint materialSlot = (VertexWords[vertexWord + 5] >> 16u) & 0xFFu;
    output.position =
        mul(mul(pushConstants.ViewProjection, proxy.worldTS), float4(localPosition, 1.0));
    output.worldNormal = mul((float3x3)proxy.worldTS, localNormal);
    output.materialId = FetchNodeMaterialId(proxy, materialSlot);
    return output;
}
