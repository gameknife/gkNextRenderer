// Compatibility raster pass (see docs/designs/ios-a12x-compatibility-minimal-render-mvp.md).
//
// Three things this shader must not do, because the target device supports none of them:
//
//   1. No `import Shader.Bindless` -- that declares the unbounded __DynamicResource arrays, whose
//      descriptor counts a constrained device cannot back.
//   2. No GPUScene push constant -- every pointer in it is a buffer device address, and the A12X
//      does not support bufferDeviceAddress at all. Dereferencing one takes the device down.
//   3. No 16-bit types at all -- reading GPUVertex as half4 makes SPIR-V require
//      UniformAndStorageBuffer16BitAccess (which the engine never enables), and even f16tof32
//      lowers to a bitcast through `half`, requiring the Float16 capability. The vertex buffer is
//      therefore read as raw uints and decoded with integer math, leaving `Shader` and
//      `DrawParameters` (from SV_VertexID) as the only capabilities this module declares.
//
// So the scene arrives through an ordinary descriptor set of storage buffers, and the only thing in
// the push constant is the camera matrix plus which proxy is being drawn. Storage buffers rather
// than a uniform buffer on purpose: std430 array stride matches the CPU-side struct layout, while
// std140 would pad NodeProxy::matId[16] from 4 to 16 bytes per element.
//
// One draw per render proxy: the host sets ProxyIndex and issues vkCmdDraw(indexCount). There is no
// GPU cull / soft-mesh expansion here, because those compute passes do bind the bindless set.
import Shader.Types;

[[vk::binding(0, 0)]] StructuredBuffer<NodeProxy> Nodes;
// GPUVertex as six raw uints; see kVertexWordCount below.
[[vk::binding(1, 0)]] StructuredBuffer<uint> VertexWords;
[[vk::binding(2, 0)]] StructuredBuffer<uint> Indices;
[[vk::binding(3, 0)]] StructuredBuffer<ModelData> Offsets;

struct CompatibilityPushConstants
{
    float4x4 ViewProjection;
    uint ProxyIndex;
};
[[vk::push_constant]] CompatibilityPushConstants pushConstants;

// GPUVertex is three half4s = 24 bytes = 6 uints (Assets::GPUVertex in Vertex.hpp is 12 uint16s).
// Word 0..1 is Position_Tx, 2..3 Normal_Ty, 4..5 Tangent.
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
        // Zero or subnormal; 2^-24 is the value of the lowest mantissa bit.
        const float magnitude = float(mantissa) * 5.9604644775390625e-8f;
        return (sign != 0u) ? -magnitude : magnitude;
    }
    if (exponent == 31u)
    {
        return asfloat(sign | 0x7F800000u | (mantissa << 13u)); // inf / NaN
    }
    // Re-bias the exponent: half bias 15 -> float bias 127.
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
    nointerpolation uint materialId : MATERIAL_ID;
};

[shader("vertex")]
VertexOutput main(uint vertexIndex: SV_VertexID)
{
    VertexOutput output;

    const NodeProxy proxy = Nodes[pushConstants.ProxyIndex];
    const ModelData model = Offsets[proxy.modelId];

    const uint localVertex = Indices[model.indexOffset + vertexIndex];
    // Skinned meshes keep their deformed copy in a separate buffer, but the skinning compute pass
    // is one of the scene-frame prepasses this profile does not run. Read the bind pose rather than
    // a buffer nothing has written this frame.
    const uint vertexWord = (model.vertexOffset + localVertex) * kVertexWordCount;

    const float2 positionXY = UnpackHalf2(VertexWords[vertexWord + 0]);
    const float2 positionZ_TexU = UnpackHalf2(VertexWords[vertexWord + 1]);
    const float3 localPosition = float3(positionXY, positionZ_TexU.x);

    // Tangent.w packs the per-vertex material slot in its low byte (see UnpackVertex): the half sits
    // in the upper 16 bits of the last word, and the slot is that half's low byte.
    const uint materialSlot = (VertexWords[vertexWord + 5] >> 16u) & 0xFFu;

    output.position =
        mul(mul(pushConstants.ViewProjection, proxy.worldTS), float4(localPosition, 1.0));
    output.materialId = FetchNodeMaterialId(proxy, materialSlot);
    return output;
}
