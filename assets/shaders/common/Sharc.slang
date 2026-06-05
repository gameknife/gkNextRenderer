implementing Common;

public struct SharcSample
{
    public uint Key;
    public uint Slot;
    public uint PackedNormal;
};

public uint SharcPackNormal(float3 normal)
{
    float3 n = normalize(normal) * 0.5f + 0.5f;
    uint x = uint(clamp(n.x, 0.0f, 1.0f) * 1023.0f);
    uint y = uint(clamp(n.y, 0.0f, 1.0f) * 1023.0f);
    uint z = uint(clamp(n.z, 0.0f, 1.0f) * 1023.0f);
    return x | (y << 10) | (z << 20);
}

public float3 SharcUnpackNormal(uint packedNormal)
{
    float3 n;
    n.x = float(packedNormal & 1023u) / 1023.0f;
    n.y = float((packedNormal >> 10) & 1023u) / 1023.0f;
    n.z = float((packedNormal >> 20) & 1023u) / 1023.0f;
    return normalize(n * 2.0f - 1.0f);
}

public uint SharcHashUint(uint x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

public uint SharcHashCell(int3 cell)
{
    uint h = uint(cell.x) * 73856093u;
    h ^= uint(cell.y) * 19349663u;
    h ^= uint(cell.z) * 83492791u;
    return SharcHashUint(h);
}

public SharcParameters SharcGetParameters()
{
    SharcResources* resources = Bindless.GetGpuscene().Sharc;
    return ((SharcParameters*)resources[0].Parameters)[0];
}

public bool SharcIsAvailable()
{
    return Bindless.GetGpuscene().Sharc != (SharcResources*)0;
}

public SharcSample SharcMakeSample(float3 worldPosition, float3 normal)
{
    SharcParameters params = SharcGetParameters();
    int3 cell = int3(floor(worldPosition / max(params.VoxelSize, 0.001f)));
    uint key = SharcHashCell(cell);
    key = key == 0u ? 1u : key;

    SharcSample sample;
    sample.Key = key;
    sample.Slot = key & params.EntryMask;
    sample.PackedNormal = SharcPackNormal(normal);
    return sample;
}

public uint SharcEncodeRadiance(float value)
{
    return uint(clamp(value, 0.0f, 64.0f) * 1024.0f);
}

public float SharcDecodeRadiance(uint value, uint sampleCount)
{
    return (float(value) / 1024.0f) / max(float(sampleCount), 1.0f);
}

public bool SharcNormalMatches(uint packedNormal, float3 normal, float threshold)
{
    return dot(SharcUnpackNormal(packedNormal), normalize(normal)) >= threshold;
}

public void SharcAccumulate(float3 worldPosition, float3 normal, float3 radiance)
{
    if (!SharcIsAvailable())
    {
        return;
    }

    SharcResources* resources = Bindless.GetGpuscene().Sharc;
    SharcParameters params = ((SharcParameters*)resources[0].Parameters)[0];
    SharcSample sample = SharcMakeSample(worldPosition, normal);

    SharcHashEntry* hashEntries = (SharcHashEntry*)resources[0].HashEntries;
    SharcAccumulationEntry* accumulation = (SharcAccumulationEntry*)resources[0].Accumulation;

    uint originalKey = 0u;
    InterlockedCompareExchange(hashEntries[sample.Slot].Key, 0u, sample.Key, originalKey);
    if (originalKey != 0u && originalKey != sample.Key)
    {
        return;
    }

    hashEntries[sample.Slot].PackedNormal = sample.PackedNormal;
    hashEntries[sample.Slot].LastFrame = params.FrameIndex;
    InterlockedAdd(accumulation[sample.Slot].RadianceR, SharcEncodeRadiance(radiance.r));
    InterlockedAdd(accumulation[sample.Slot].RadianceG, SharcEncodeRadiance(radiance.g));
    InterlockedAdd(accumulation[sample.Slot].RadianceB, SharcEncodeRadiance(radiance.b));
    InterlockedAdd(accumulation[sample.Slot].SampleCount, 1u);
}

public bool SharcQuery(float3 worldPosition, float3 normal, out float4 radiance)
{
    radiance = float4(0, 0, 0, 0);
    if (!SharcIsAvailable())
    {
        return false;
    }

    SharcResources* resources = Bindless.GetGpuscene().Sharc;
    SharcParameters params = ((SharcParameters*)resources[0].Parameters)[0];
    SharcSample sample = SharcMakeSample(worldPosition, normal);

    SharcHashEntry* hashEntries = (SharcHashEntry*)resources[0].HashEntries;
    SharcResolvedEntry* resolved = (SharcResolvedEntry*)resources[0].Resolved;
    SharcHashEntry hashEntry = hashEntries[sample.Slot];
    SharcResolvedEntry resolvedEntry = resolved[sample.Slot];

    if (hashEntry.Key != sample.Key || resolvedEntry.SampleCount == 0u)
    {
        return false;
    }
    if (!SharcNormalMatches(resolvedEntry.PackedNormal, normal, 0.35f))
    {
        return false;
    }
    if (params.FrameIndex > resolvedEntry.LastFrame + params.StaleFrameCount)
    {
        return false;
    }

    radiance = resolvedEntry.Radiance;
    return true;
}

