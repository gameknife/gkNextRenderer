import Shader.Bindless;
import Shader.Types;
import Shader.GpuScene;

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint idx = DTid.x;
    // custom_data_0 = cube-pool element count, custom_data_2 = residency entry count.
    uint cubeTotal = Bindless.GetGpuscene().custom_data_0;
    uint residencyTotal = Bindless.GetGpuscene().custom_data_2;

    AmbientCube* Cubes = Bindless.GetGpuscene().Cubes;
    AmbientBrickResidency* Residency = Bindless.GetGpuscene().AmbientResidency;

    if (idx < cubeTotal)
    {
        Cubes[idx].PosZ = 0;
        Cubes[idx].NegZ = 0;
        Cubes[idx].PosY = 0;
        Cubes[idx].NegY = 0;
        Cubes[idx].PosX = 0;
        Cubes[idx].NegX = 0;
        Cubes[idx].SunDirect = 0;
        Cubes[idx].SkyEmissiveDirect = 0;
        Cubes[idx].skyVisibility_pznzpyny = 0;
        Cubes[idx].skyVisibility_pxnxs0s1 = 0;
    }

    if (idx < residencyTotal)
    {
        Residency[idx].lastConsumerHitFrame = 0;
        Residency[idx].lastBounceHitFrame = 0;
        Residency[idx].consumerHitCount = 0;
        Residency[idx].bounceHitCount = 0;
    }
}
