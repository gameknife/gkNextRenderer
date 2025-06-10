import Common;

[[vk::binding(0, 0)]]
RWTexture2D<float4> OutImage;

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    int2 ipos = int2(dispatchThreadId.xy);
    OutImage[ipos] = float4(0, 0, 0, 0);
}