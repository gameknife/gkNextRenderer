// Compile-only probe for the 3D bindless path (GK_BUILD_SHADER_TESTS).
//
// Guards the one thing the runtime path cannot check for itself: that slangc still lowers
// `__DynamicResource.as<RWTexture3D<T>>()` / `.as<Sampler3D>()` into valid SPIR-V, and that 3D
// and 2D resources use separate descriptor bindings in one module. This is not
// dispatched by anything -- Test_Bindless3DTexture.cpp covers runtime behaviour.
//
// Expected SPIR-V: 2D resources remain on bindings 0/1, while 3D resources use bindings 3/4,
// all decorated NonUniform.

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

    // 2D access in the same module uses the regular 2D descriptor arrays.
    const uint2 dim2d = Bindless.GetStorageTextureDimensions<float4>(Bindless.RT_SCENE_COLOR);
    if (all(dispatchThreadId.xy < dim2d))
    {
        const float4 tex2d = Bindless.GetSampleTexture(volumeSlot).SampleLevel(uvw.xy, 0.0f);
        Bindless.GetStorageTexture3D<float4>(volumeSlot)[dispatchThreadId] = sampled + tex2d;
    }
}
