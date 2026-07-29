import Shader.Bindless;
import Shader.PathTracing;

float4 AmbientCubeClearSky()
{
    UniformBufferObject camera = Bindless.GetGpuscene().Camera[0];
    if (!camera.HasSky && !Common.AtmosphereEnabled(camera))
    {
        return float4(0.0f);
    }

    float4 sky = float4(0.0f);
    sky += Common.SampleSkyRadiance(float3(0.0f, 0.0f, 1.0f), 1.0f);
    sky += Common.SampleSkyRadiance(float3(0.0f, 0.0f, -1.0f), 1.0f);
    sky += Common.SampleSkyRadiance(float3(0.0f, 1.0f, 0.0f), 1.0f);
    sky += Common.SampleSkyRadiance(float3(0.0f, -1.0f, 0.0f), 1.0f);
    sky += Common.SampleSkyRadiance(float3(1.0f, 0.0f, 0.0f), 1.0f);
    sky += Common.SampleSkyRadiance(float3(-1.0f, 0.0f, 0.0f), 1.0f);
    return sky / 6.0f;
}

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint idx = DTid.x;
    // CustomData0 = cube-pool element count, CustomData2 = residency entry count.
    uint cubeTotal = Bindless.GetGpuscene().CustomData0;
    uint residencyTotal = Bindless.GetGpuscene().CustomData2;

    AmbientCube* Cubes = Bindless.GetGpuscene().Cubes;
    AmbientBrickResidency* Residency = Bindless.GetGpuscene().AmbientResidency;
    float4 clearSky = AmbientCubeClearSky();

    if (idx < cubeTotal)
    {
        Cubes[idx].PosZ = 0;
        Cubes[idx].NegZ = 0;
        Cubes[idx].PosY = 0;
        Cubes[idx].NegY = 0;
        Cubes[idx].PosX = 0;
        Cubes[idx].NegX = 0;
        Cubes[idx].SunDirect = 0;
        Cubes[idx].SkyEmissiveDirect = packRGB9E5(clearSky.rgb);
        Cubes[idx].SkyVisibilityPZNZPYNY = PackBytes(uint4(255u, 255u, 255u, 255u));
        Cubes[idx].SkyVisibilityPXNXS0S1 = PackBytes(uint4(255u, 255u, 0u, 0u));
    }

    if (idx < residencyTotal)
    {
        Residency[idx].lastConsumerHitFrame = 0;
        Residency[idx].lastBounceHitFrame = 0;
        Residency[idx].consumerHitCount = 0;
        Residency[idx].bounceHitCount = 0;
    }
}
