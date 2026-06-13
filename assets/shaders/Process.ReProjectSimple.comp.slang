import Common;
import Bindless;

struct PushConsts
{
    uint TemporalFrames;
    uint PrevDiffuseBindlessIdx;
    uint HistoryValid;
    uint TAAEnabled;
};

[[vk::push_constant]]
ConstantBuffer<PushConsts> pushConsts;

static const uint OBJECT_ID_FLAG_VALID = 0x80000000u;
static const uint OBJECT_ID_INSTANCE_MASK = 0x07FFFFFFu;
static const uint OBJECT_ID_INVALID_INSTANCE = 0xFFFFFFFFu;

uint FetchPrimitiveIndex(uint objectId)
{
    return (objectId & OBJECT_ID_FLAG_VALID) != 0
        ? (objectId & OBJECT_ID_INSTANCE_MASK)
        : OBJECT_ID_INVALID_INSTANCE;
}

float3 LoadHistorySample(
    RWTexture2D<float4> historyImage,
    RWTexture2D<uint> previousObjectIdImage,
    int2 position,
    uint currentPrimitiveIndex,
    float3 currentColor)
{
    return FetchPrimitiveIndex(previousObjectIdImage[position]) == currentPrimitiveIndex
        ? historyImage[position].rgb
        : currentColor;
}

float3 FilterHistoryColor(
    RWTexture2D<float4> historyImage,
    RWTexture2D<uint> previousObjectIdImage,
    float2 historyPosition,
    int2 basePosition,
    uint currentPrimitiveIndex,
    float3 currentColor)
{
    float2 subpixel = frac(historyPosition);
    float3 sample00 = LoadHistorySample(
        historyImage, previousObjectIdImage, basePosition, currentPrimitiveIndex, currentColor);
    float3 sample10 = LoadHistorySample(
        historyImage, previousObjectIdImage, basePosition + int2(1, 0), currentPrimitiveIndex, currentColor);
    float3 sample01 = LoadHistorySample(
        historyImage, previousObjectIdImage, basePosition + int2(0, 1), currentPrimitiveIndex, currentColor);
    float3 sample11 = LoadHistorySample(
        historyImage, previousObjectIdImage, basePosition + int2(1, 1), currentPrimitiveIndex, currentColor);

    return lerp(lerp(sample00, sample10, subpixel.x),
                lerp(sample01, sample11, subpixel.x), subpixel.y);
}

float3 ClampHistoryToNeighborhood(
    RWTexture2D<float4> currentImage,
    int2 position,
    int2 imageMax,
    float3 history)
{
    float3 aabbMin = float3(1e30f);
    float3 aabbMax = float3(-1e30f);

    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            int2 samplePosition = clamp(position + int2(x, y), int2(0), imageMax);
            float3 currentYCoCg = rgb2ycocg(currentImage[samplePosition].rgb);
            aabbMin = min(aabbMin, currentYCoCg);
            aabbMax = max(aabbMax, currentYCoCg);
        }
    }

    return ycocg2rgb(clamp(rgb2ycocg(history), aabbMin, aabbMax));
}

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint2 size = Bindless.GetStorageTextureDimensions<float4>(Bindless.RT_SINGLE_DIFFUSE);
    if (DTid.x >= size.x || DTid.y >= size.y)
    {
        return;
    }

    int2 position = int2(DTid.xy);
    int2 imageSize = int2(size);
    int2 imageMax = imageSize - int2(1);

    let CurrentImage = Bindless.GetStorageTexture<float4>(Bindless.RT_SINGLE_DIFFUSE);
    let HistoryImage = Bindless.GetStorageTexture<float4>(pushConsts.PrevDiffuseBindlessIdx);
    let AccumulatedImage = Bindless.GetStorageTexture<float4>(Bindless.RT_ACCUMLATE_DIFFUSE);
    let CurrentObjectIdImage = Bindless.GetStorageTexture<uint>(Bindless.RT_OBJEDCTID_0);
    let PreviousObjectIdImage = Bindless.GetStorageTexture<uint>(Bindless.RT_OBJEDCTID_1);
    let MotionVectorImage = Bindless.GetStorageTexture<float4>(Bindless.RT_MOTIONVECTOR);
    let MotionMomentImage = Bindless.GetStorageTexture<uint>(Bindless.RT_MOTIONMOMENT);

    float3 current = CurrentImage[position].rgb;
    float2 historyPosition = float2(position) + MotionVectorImage[position].rg;
    int2 previousPosition = int2(floor(historyPosition));
    bool inside = all(previousPosition >= int2(0)) &&
                  all(previousPosition + int2(1) < imageSize);

    uint currentPrimitiveIndex = FetchPrimitiveIndex(CurrentObjectIdImage[position]);
    bool useHistory = pushConsts.HistoryValid != 0 &&
                      pushConsts.TAAEnabled != 0 &&
                      currentPrimitiveIndex != OBJECT_ID_INVALID_INSTANCE &&
                      MotionMomentImage[position] == 0 &&
                      inside;

    if (useHistory)
    {
        useHistory = FetchPrimitiveIndex(PreviousObjectIdImage[previousPosition]) == currentPrimitiveIndex;
    }

    float3 result = current;
    if (useHistory)
    {
        float3 history = FilterHistoryColor(
            HistoryImage, PreviousObjectIdImage, historyPosition, previousPosition,
            currentPrimitiveIndex, current);
        history = ClampHistoryToNeighborhood(CurrentImage, position, imageMax, history);

        float currentWeight = 1.0f / max(1u, pushConsts.TemporalFrames);
        result = lerp(history, current, saturate(currentWeight));
    }

    AccumulatedImage[position] = float4(result, 1.0f);
}
