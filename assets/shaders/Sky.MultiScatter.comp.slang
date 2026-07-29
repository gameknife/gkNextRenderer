import Shader.Bindless;
import Shader.Types;
import Shader.GpuScene;

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const UniformBufferObject camera = Bindless.GetGpuscene().Camera[0];
    if (camera.AtmosphereParams == 0)
    {
        return;
    }
    const FAtmosphereParams params = ((FAtmosphereParams*)camera.AtmosphereParams)[0];
    const uint2 size = Bindless.GetStorageTextureDimensions<float4>(params.MultiScatterLutId);
    if (any(dispatchThreadId.xy >= size))
    {
        return;
    }
    const float2 uv = (float2(dispatchThreadId.xy) + 0.5f) / float2(size);
    const float3 transmittance =
        Bindless.GetSampleTexture(params.TransmittanceLutId).SampleLevel(uv, 0.0f).rgb;
    const float3 scatteringAlbedo =
        (params.RayleighScattering + params.MieScattering) /
        max(params.RayleighScattering + params.MieScattering + params.MieAbsorption, float3(1.0e-4f));
    const float groundBounce = dot(params.GroundAlbedo, float3(0.2126f, 0.7152f, 0.0722f));
    const float3 multiScatter =
        (1.0f - transmittance) * scatteringAlbedo * (0.12f + 0.08f * groundBounce);
    Bindless.GetStorageTexture<float4>(params.MultiScatterLutId)[dispatchThreadId.xy] =
        float4(max(multiScatter, float3(0.0f)), 1.0f);
}
