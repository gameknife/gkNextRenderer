// Pass A of the 3D bindless runtime check (see src/Tests/Test_Bindless3DTexture.cpp).
// Writes a known ramp into a 3D storage image bound in the global bindless array:
//     value(x, y, z) = x + y * dim.x + z * dim.x * dim.y

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
    const uint3 dim = Bindless.GetStorageTexture3DDimensions<float>(volumeSlot);
    if (any(dispatchThreadId >= dim))
    {
        return;
    }

    const float value = float(dispatchThreadId.x) +
                        float(dispatchThreadId.y) * float(dim.x) +
                        float(dispatchThreadId.z) * float(dim.x * dim.y);
    Bindless.GetStorageTexture3D<float>(volumeSlot)[dispatchThreadId] = value;
}
