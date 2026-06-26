import Bindless;
import Common;

struct PushConsts
{
    // Unified push-constant header (offset 0..15): aliases gpuScene's header so
    // gpuScene.custom_data_0 (active view RT bank base) is valid in this custom pass. Params follow.
    uint _ViewHdr_SwapChainIndex;
    uint _ViewHdr_custom_data_0;
    uint _ViewHdr_custom_data_1;
    uint _ViewHdr_custom_data_2;

    uint SwapChainIdx;
};
[[vk::push_constant]] ConstantBuffer<PushConsts> pushConsts;

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    int2 ipos = int2(dispatchThreadId.xy);
    //Bindless.GetStorageTexture<float4>(Bindless.RT_SWAPCHAIN0 + SwapChainIdx)[ipos] = float4(0, 0, 0, 0);
    Bindless.GetStorageTexture<float4>(Bindless.ViewRT(pushConsts._ViewHdr_custom_data_0, Bindless.RT_SINGLE_DIFFUSE))[ipos] = float4(0, 0, 0, 0);
    Bindless.GetStorageTexture<float4>(Bindless.ViewRT(pushConsts._ViewHdr_custom_data_0, Bindless.RT_SINGLE_SPECULAR))[ipos] = float4(0, 0, 0, 0);
    Bindless.GetStorageTexture<float>(Bindless.ViewRT(pushConsts._ViewHdr_custom_data_0, Bindless.RT_PREV_DEPTHBUFFER))[ipos] = 100000.0f;
}