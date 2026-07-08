import Shader.Types;

struct AuxPrimitive
{
    float4 p0Type;
    float4 p1Size;
    float4 color;
    uint4 params;
};

struct PushConstants
{
    uint64_t cameraAddress;
    uint64_t primitiveAddress;
    uint primitiveCount;
    uint outputWidth;
    uint outputHeight;
    uint padding;
};

[[vk::push_constant]] PushConstants push;

static const uint AUX_TYPE_LINE = 0;
static const uint AUX_TYPE_POINT = 1;
static const uint AUX_FLAG_DEPTH_TEST = 1u << 0u;

struct VertexOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float4 color : COLOR0;
    [[vk::location(1)]] nointerpolation float4 lineStartRadius : TEXCOORD0;
    [[vk::location(2)]] nointerpolation float4 lineEndType : TEXCOORD1;
    [[vk::location(3)]] nointerpolation uint flags : TEXCOORD2;
};

bool ProjectWorld(
    UniformBufferObject camera,
    float3 worldPosition,
    float2 outputSize,
    out float2 pixelPosition,
    out float depth)
{
    const float4 clip = mul(camera.ViewProjection, float4(worldPosition, 1.0));
    if (clip.w <= 1e-5)
    {
        pixelPosition = float2(0.0);
        depth = 0.0;
        return false;
    }

    const float3 ndc = clip.xyz / clip.w;
    if (ndc.z < 0.0 || ndc.z > 1.0)
    {
        pixelPosition = float2(0.0);
        depth = 0.0;
        return false;
    }

    pixelPosition = (ndc.xy * 0.5 + 0.5) * outputSize;
    depth = ndc.z;
    return true;
}

float4 PixelToClip(float2 pixel, float depth, float2 outputSize)
{
    const float2 ndc = pixel / outputSize * 2.0 - 1.0;
    return float4(ndc, depth, 1.0);
}

float2 QuadCorner(uint vertexInPrimitive)
{
    switch (vertexInPrimitive)
    {
    case 0: return float2(-1.0, -1.0);
    case 1: return float2( 1.0, -1.0);
    case 2: return float2(-1.0,  1.0);
    case 3: return float2(-1.0,  1.0);
    case 4: return float2( 1.0, -1.0);
    default: return float2(1.0, 1.0);
    }
}

[shader("vertex")]
VertexOutput main(uint vertexId : SV_VertexID)
{
    VertexOutput output;
    output.position = float4(2.0, 2.0, 0.0, 1.0);
    output.color = float4(0.0);
    output.lineStartRadius = float4(0.0);
    output.lineEndType = float4(0.0);
    output.flags = 0;

    const uint primitiveIndex = vertexId / 6;
    if (primitiveIndex >= push.primitiveCount)
    {
        return output;
    }

    UniformBufferObject* cameraPtr = (UniformBufferObject*)push.cameraAddress;
    AuxPrimitive* primitives = (AuxPrimitive*)push.primitiveAddress;
    const UniformBufferObject camera = cameraPtr[0];
    const AuxPrimitive primitive = primitives[primitiveIndex];
    const uint type = primitive.params.x;
    const uint flags = primitive.params.y;
    const float2 outputSize = float2(push.outputWidth, push.outputHeight);

    float2 startPixel;
    float2 endPixel;
    float startDepth;
    float endDepth;
    if (!ProjectWorld(camera, primitive.p0Type.xyz, outputSize, startPixel, startDepth))
    {
        return output;
    }

    float radius = max(primitive.p1Size.w, 1.0);
    if (type == AUX_TYPE_LINE)
    {
        if (!ProjectWorld(camera, primitive.p1Size.xyz, outputSize, endPixel, endDepth))
        {
            return output;
        }
        radius *= 0.5;
    }
    else if (type == AUX_TYPE_POINT)
    {
        endPixel = startPixel;
        endDepth = startDepth;
    }
    else
    {
        return output;
    }

    const float2 corner = QuadCorner(vertexId % 6);
    float2 pixel = startPixel;
    float depth = startDepth;
    if (type == AUX_TYPE_LINE)
    {
        const float2 segment = endPixel - startPixel;
        const float segmentLength = length(segment);
        const float2 direction = segmentLength > 1e-5 ? segment / segmentLength : float2(1.0, 0.0);
        const float2 perpendicular = float2(-direction.y, direction.x);
        const float t = corner.x > 0.0 ? 1.0 : 0.0;
        pixel = lerp(startPixel, endPixel, t) + perpendicular * corner.y * (radius + 1.0);
        depth = lerp(startDepth, endDepth, t);
    }
    else
    {
        pixel = startPixel + corner * (radius + 1.0);
        depth = startDepth;
    }

    if ((flags & AUX_FLAG_DEPTH_TEST) == 0u)
    {
        depth = 0.0;
    }

    output.position = PixelToClip(pixel, depth, outputSize);
    output.color = primitive.color;
    output.lineStartRadius = float4(startPixel, startDepth, radius);
    output.lineEndType = float4(endPixel, endDepth, float(type));
    output.flags = flags;
    return output;
}
