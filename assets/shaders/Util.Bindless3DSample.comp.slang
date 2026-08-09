// Pass B of the 3D bindless runtime check (see src/Tests/Test_Bindless3DTexture.cpp).
// Samples the volume written by Util.Bindless3DWrite through a Sampler3D and writes the taps into
// a host-visible buffer, so the test can assert both exact texel fetches and trilinear blends.

import Shader.Bindless;

struct PushConsts
{
    // Unified push-constant header (offset 0..15), see ZeroBindCustomPushConstantPipeline.
    uint _ViewHdr_SwapChainIndex;
    uint _ViewHdr_CustomData0;
    uint _ViewHdr_CustomData1;
    uint _ViewHdr_CustomData2;

    uint VolumeSlot;
    uint SampleCount;
    uint Pad0;
    uint Pad1;
    // float3 uvw per tap, padded to float4.
    uint64_t CoordAddress;
    // one float result per tap.
    uint64_t ResultAddress;
};
[[vk::push_constant]] ConstantBuffer<PushConsts> pushConsts;

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint tap = dispatchThreadId.x;
    if (tap >= pushConsts.SampleCount)
    {
        return;
    }

    float4* coords = (float4*)pushConsts.CoordAddress;
    float* results = (float*)pushConsts.ResultAddress;

    const float3 uvw = coords[tap].xyz;
    results[tap] = Bindless.GetSampleTexture3D(int(pushConsts.VolumeSlot)).SampleLevel(uvw, 0.0f).x;
}
