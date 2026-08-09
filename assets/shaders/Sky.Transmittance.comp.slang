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
    const uint2 size = Bindless.GetStorageTextureDimensions<float4>(params.TransmittanceLutId);
    if (any(dispatchThreadId.xy >= size))
    {
        return;
    }

    const float2 uv = (float2(dispatchThreadId.xy) + 0.5f) / float2(size);
    const float altitude = uv.y * (params.TopRadius - params.BottomRadius);
    const float viewZenithCosine = uv.x * 2.0f - 1.0f;
    const float3 origin = float3(0.0f, params.BottomRadius + altitude, 0.0f);
    const float3 direction = normalize(float3(
        sqrt(max(1.0f - viewZenithCosine * viewZenithCosine, 0.0f)),
        viewZenithCosine, 0.0f));
    const float distance = Atmosphere.RaySphereNearest(origin, direction, params.TopRadius);
    const float3 transmittance = distance > 0.0f
        ? Atmosphere.IntegrateTransmittance(params, origin, direction, distance, 40)
        : float3(0.0f);
    Bindless.GetStorageTexture<float4>(params.TransmittanceLutId)[dispatchThreadId.xy] =
        float4(transmittance, 1.0f);
}
