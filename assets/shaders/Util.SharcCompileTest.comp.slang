import Common;
import Bindless;

[shader("compute")]
[numthreads(1, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (!SharcIsAvailable())
    {
        return;
    }

    float3 radiance = float3(0, 0, 0);
    float4 cachedRadiance = float4(0, 0, 0, 0);
    SharcAccumulate(float3(0, 0, 0), float3(0, 1, 0), radiance);
    SharcQuery(float3(0, 0, 0), float3(0, 1, 0), cachedRadiance);
    SharcResolveOfficialEntry(DTid.x);
}
