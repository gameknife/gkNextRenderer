import Common;
import Bindless;

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (!SharcIsAvailable())
    {
        return;
    }

    SharcResources* resources = Bindless.GetGpuscene().Sharc;
    SharcParameters params = ((SharcParameters*)resources[0].Parameters)[0];
    uint entryIndex = DTid.x;
    if (entryIndex >= params.EntryCount)
    {
        return;
    }

    SharcHashEntry* hashEntries = (SharcHashEntry*)resources[0].HashEntries;
    SharcAccumulationEntry* accumulation = (SharcAccumulationEntry*)resources[0].Accumulation;
    SharcResolvedEntry* resolved = (SharcResolvedEntry*)resources[0].Resolved;

    SharcAccumulationEntry accum = accumulation[entryIndex];
    SharcResolvedEntry oldValue = resolved[entryIndex];
    uint hashKey = hashEntries[entryIndex].Key;

    if (hashKey == 0u)
    {
        resolved[entryIndex].Radiance = float4(0, 0, 0, 0);
        resolved[entryIndex].SampleCount = 0u;
        return;
    }

    if (params.FrameIndex > oldValue.LastFrame + params.StaleFrameCount)
    {
        hashEntries[entryIndex].Key = 0u;
        hashEntries[entryIndex].PackedNormal = 0u;
        hashEntries[entryIndex].LastFrame = 0u;
        resolved[entryIndex].Radiance = float4(0, 0, 0, 0);
        resolved[entryIndex].PackedNormal = 0u;
        resolved[entryIndex].LastFrame = 0u;
        resolved[entryIndex].SampleCount = 0u;
    }

    if (accum.SampleCount > 0u)
    {
        float invSamples = 1.0f / float(accum.SampleCount);
        float4 newRadiance = float4(float(accum.RadianceR) / 1024.0f,
                                    float(accum.RadianceG) / 1024.0f,
                                    float(accum.RadianceB) / 1024.0f,
                                    1.0f) * invSamples;
        float historyWeight = oldValue.SampleCount == 0u ? 0.0f : 0.9f;
        resolved[entryIndex].Radiance = lerp(newRadiance, oldValue.Radiance, historyWeight);
        resolved[entryIndex].PackedNormal = hashEntries[entryIndex].PackedNormal;
        resolved[entryIndex].LastFrame = params.FrameIndex;
        resolved[entryIndex].SampleCount = min(oldValue.SampleCount + accum.SampleCount, 65535u);
    }

    accumulation[entryIndex].RadianceR = 0u;
    accumulation[entryIndex].RadianceG = 0u;
    accumulation[entryIndex].RadianceB = 0u;
    accumulation[entryIndex].SampleCount = 0u;
}

