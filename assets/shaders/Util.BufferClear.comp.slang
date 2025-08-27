import Bindless;

[[vk::push_constant]] ConstantBuffer<uint> SwapChainIdx;

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    int2 ipos = int2(dispatchThreadId.xy);
    Bindless.GetStorageTexture<float4>(Bindless.RT_SWAPCHAIN0 + SwapChainIdx)[ipos] = float4(0, 0, 0, 0);
    Bindless.GetStorageTexture<float4>(Bindless.RT_SINGLE_DIFFUSE)[ipos] = float4(0, 0, 0, 0);
    Bindless.GetStorageTexture<float4>(Bindless.RT_SINGLE_SPECULAR)[ipos] = float4(0, 0, 0, 0);
    Bindless.GetStorageTexture<float>(Bindless.RT_PREV_DEPTHBUFFER)[ipos] = 100000.0f;
}