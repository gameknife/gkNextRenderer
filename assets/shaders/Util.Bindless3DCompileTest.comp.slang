// Compile-only probe for the 3D bindless path (GK_BUILD_SHADER_TESTS).
//
// Guards the one thing the runtime path cannot check for itself: that slangc still lowers
// `__DynamicResource.as<RWTexture3D<T>>()` / `.as<Sampler3D>()` into valid SPIR-V, and that a
// 3D and a 2D alias of the *same* descriptor binding can coexist in one module. This is not
// dispatched by anything -- Test_Bindless3DTexture.cpp covers runtime behaviour.
//
// Expected SPIR-V: two OpTypeImage aliases (Dim2D and Dim3D) on each of (set=0, binding=0) and
// (set=0, binding=1), all decorated NonUniform.

import Shader.Bindless;

struct PushConsts
{
    // Unified push-constant header (offset 0..15), see ZeroBindCustomPushConstantPipeline.
    uint _ViewHdr_SwapChainIndex;
    uint _ViewHdr_CustomData0;
    uint _ViewHdr_CustomData1;
    uint _ViewHdr_CustomData2;

    uint VolumeSlot;
};
[[vk::push_constant]] ConstantBuffer<PushConsts> pushConsts;

[shader("compute")]
[numthreads(4, 4, 4)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const int volumeSlot = int(pushConsts.VolumeSlot);

    // 3D query + 3D storage write.
    const uint3 dim = Bindless.GetStorageTexture3DDimensions<float4>(volumeSlot);
    if (any(dispatchThreadId >= dim))
    {
        return;
    }

    // 3D sampled read with explicit LOD.
    const float3 uvw = (float3(dispatchThreadId) + 0.5f) / float3(dim);
    const float4 sampled = Bindless.GetSampleTexture3D(volumeSlot).SampleLevel(uvw, 0.0f);

    Bindless.GetStorageTexture3D<float4>(volumeSlot)[dispatchThreadId] = sampled;

    // 2D alias of the very same descriptor arrays, in the same module.
    const uint2 dim2d = Bindless.GetStorageTextureDimensions<float4>(Bindless.RT_SCENE_COLOR);
    if (all(dispatchThreadId.xy < dim2d))
    {
        const float4 tex2d = Bindless.GetSampleTexture(volumeSlot).SampleLevel(uvw.xy, 0.0f);
        Bindless.GetStorageTexture3D<float4>(volumeSlot)[dispatchThreadId] = sampled + tex2d;
    }
}
