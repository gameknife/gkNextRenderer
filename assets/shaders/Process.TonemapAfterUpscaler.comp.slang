import Shader.Bindless;
import Shader.Types;
import Shader.Tonemap;

struct PushConsts
{
    // Unified push-constant header used by zero-bind compute pipelines.
    uint _ViewHdr_SwapChainIndex;
    uint _ViewHdr_CustomData0;
    uint _ViewHdr_CustomData1;
    uint _ViewHdr_CustomData2;

    uint InputIndex;
    uint InputWidth;
    uint InputHeight;
    uint OutputWidth;
    uint OutputHeight;
    int OutputOffsetX;
    int OutputOffsetY;
    uint OutputIndex;
    uint SourceIsUpscaled;
    float PaperWhiteNit;
    uint HDROutputMode;
    uint HDR;
    uint HasSky;
};

[[vk::push_constant]]
ConstantBuffer<PushConsts> pushConsts;

static const uint OBJECT_ID_FLAG_VALID = 0x80000000u;

bool HasValidObject(uint objectId)
{
    return (objectId & OBJECT_ID_FLAG_VALID) != 0u;
}

int2 ClampPixel(int2 pixel, uint2 size)
{
    return clamp(pixel, int2(0, 0), int2(size) - int2(1, 1));
}

float4 SampleBilinear(RWTexture2D<float4> source, uint2 sourceSize, float2 uv)
{
    const float2 position = uv * float2(sourceSize) - 0.5f;
    const int2 base = int2(floor(position));
    const float2 fraction = frac(position);
    const int2 size = int2(sourceSize);
    const float4 top = lerp(
        source[ClampPixel(base, sourceSize)],
        source[ClampPixel(base + int2(1, 0), sourceSize)],
        fraction.x);
    const float4 bottom = lerp(
        source[ClampPixel(base + int2(0, 1), sourceSize)],
        source[ClampPixel(base + int2(1, 1), sourceSize)],
        fraction.x);
    return lerp(top, bottom, fraction.y);
}

float3 EncodeDisplayColor(
    float3 linearColor,
    float paperWhiteNit,
    uint hdr,
    uint outputMode,
    bool noSkyBackground)
{
    if (hdr != 0u)
    {
        return noSkyBackground
            ? EncodeNoSkyBackground(true, paperWhiteNit, outputMode)
            : EncodeHdrOutput(
                  linearColor / max(paperWhiteNit / 40000.0, 1.0e-6),
                  paperWhiteNit,
                  outputMode);
    }

    return noSkyBackground
        ? EncodeNoSkyBackground(false, paperWhiteNit, outputMode)
        : GT_Tonemapping(linearColor);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint2 outputPixel = dispatchThreadId.xy;
    if (any(outputPixel >= uint2(pushConsts.OutputWidth, pushConsts.OutputHeight)))
    {
        return;
    }

    const uint2 sourceSize = uint2(pushConsts.InputWidth, pushConsts.InputHeight);
    const float2 uv = (float2(outputPixel) + 0.5f) /
        float2(pushConsts.OutputWidth, pushConsts.OutputHeight);
    const int sourceIndex = pushConsts.SourceIsUpscaled != 0u
        ? int(pushConsts.InputIndex)
        : Bindless.ViewRT(pushConsts._ViewHdr_CustomData0, int(pushConsts.InputIndex));
    const float4 sourceColor = SampleBilinear(
        Bindless.GetStorageTexture<float4>(sourceIndex), sourceSize, uv);

    // Only the original scene color carries the renderer's sky/miss alpha contract. Temporal
    // upscalers are free to use alpha for confidence or history weight (SGSR2 does exactly that).
    bool noSkyBackground = pushConsts.SourceIsUpscaled == 0u && sourceColor.a < 0.5f;
    if (!noSkyBackground && pushConsts.HasSky == 0u)
    {
        const int objectIdIndex = Bindless.ViewRT(
            pushConsts._ViewHdr_CustomData0, Bindless.RT_OBJECTID_0);
        let objectId = Bindless.GetStorageTexture<uint>(objectIdIndex);
        const uint2 objectIdSize = Bindless.GetStorageTextureDimensions<uint>(objectIdIndex);
        const int2 objectPixel = ClampPixel(
            int2((float2(outputPixel) + 0.5f) * float2(objectIdSize) /
                 float2(pushConsts.OutputWidth, pushConsts.OutputHeight)),
            objectIdSize);
        noSkyBackground = !HasValidObject(objectId[objectPixel]);
    }

    const float3 displayColor = EncodeDisplayColor(
        sourceColor.rgb,
        pushConsts.PaperWhiteNit,
        pushConsts.HDR,
        pushConsts.HDROutputMode,
        noSkyBackground);
    let output = Bindless.GetStorageTexture<float4>(
        pushConsts.OutputIndex);
    output[outputPixel + int2(pushConsts.OutputOffsetX, pushConsts.OutputOffsetY)] =
        float4(displayColor, 1.0f);
}
