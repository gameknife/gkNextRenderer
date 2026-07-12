import Shader.Bindless;

struct PushConsts
{
    // Unified push-constant header (offset 0..15): aliases gpuScene's header so
    // gpuScene.CustomData0 (active view RT bank base) is valid in this custom pass. Params follow.
    uint _ViewHdr_SwapChainIndex;
    uint _ViewHdr_CustomData0;
    uint _ViewHdr_CustomData1;
    uint _ViewHdr_CustomData2;

    uint SwapChainIdx;
};
[[vk::push_constant]] ConstantBuffer<PushConsts> pushConsts;

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    int2 ipos = int2(dispatchThreadId.xy);
    uint2 size = Bindless.GetStorageTextureDimensions<float4>(Bindless.ViewRT(pushConsts._ViewHdr_CustomData0, Bindless.RT_SINGLE_DIFFUSE));
    if (dispatchThreadId.x >= size.x || dispatchThreadId.y >= size.y)
    {
        return;
    }
    //Bindless.GetStorageTexture<float4>(Bindless.RT_SWAPCHAIN0 + SwapChainIdx)[ipos] = float4(0, 0, 0, 0);
    Bindless.GetStorageTexture<float4>(Bindless.ViewRT(pushConsts._ViewHdr_CustomData0, Bindless.RT_SINGLE_DIFFUSE))[ipos] = float4(0, 0, 0, 0);
    Bindless.GetStorageTexture<float4>(Bindless.ViewRT(pushConsts._ViewHdr_CustomData0, Bindless.RT_SINGLE_SPECULAR))[ipos] = float4(0, 0, 0, 0);
    Bindless.GetStorageTexture<float>(Bindless.ViewRT(pushConsts._ViewHdr_CustomData0, Bindless.RT_PREV_DEPTHBUFFER))[ipos] = 100000.0f;
}
