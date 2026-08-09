#ifndef GK_ATMOSPHERE_SLANG
#define GK_ATMOSPHERE_SLANG

#ifdef __cplusplus
#define ATMOS_ALIGN_16 alignas(16)
#else
#define ATMOS_ALIGN_16
#endif

public static const uint ATMOSPHERE_FLAG_SKY = 1u << 0u;
public static const uint ATMOSPHERE_FLAG_AERIAL_PERSPECTIVE = 1u << 1u;
public static const uint ATMOSPHERE_FLAG_HEIGHT_FOG = 1u << 2u;

// Shared CPU/GPU contract. Distances in the physical atmosphere model are kilometres;
// AerialPerspectiveMaxDistance and fog distances/heights remain in engine world units.
public struct ATMOS_ALIGN_16 FAtmosphereParams
{
    public float3 RayleighScattering;   public float RayleighDensityH;
    public float3 MieScattering;        public float MieDensityH;
    public float3 MieAbsorption;        public float MiePhaseG;
    public float3 OzoneAbsorption;      public float OzoneCenterAltitude;
    public float3 GroundAlbedo;         public float OzoneWidth;

    public float BottomRadius;
    public float TopRadius;
    public float WorldUnitsPerKm;
    public float WorldOriginAltitude;

    public uint TransmittanceLutId;
    public uint MultiScatterLutId;
    public uint SkyViewLutId;
    public uint AerialPerspectiveLutId;

    public float AerialPerspectiveMaxDistance;
    public float SkyLuminanceScale;
    public uint Flags;
    public float Pad0;

    public float3 FogInscatteringColor; public float FogDensity;
    public float FogHeightFalloff;      public float FogBaseHeight;
    public float FogStartDistance;      public float FogMaxOpacity;
};

#ifndef __cplusplus
namespace Atmosphere
{
    public float SafeSqrt(float value)
    {
        return sqrt(max(value, 0.0f));
    }

    public float RaySphereNearest(float3 origin, float3 direction, float radius)
    {
        const float b = dot(origin, direction);
        const float c = dot(origin, origin) - radius * radius;
        const float discriminant = b * b - c;
        if (discriminant < 0.0f)
        {
            return -1.0f;
        }
        const float root = SafeSqrt(discriminant);
        const float nearT = -b - root;
        const float farT = -b + root;
        return nearT >= 0.0f ? nearT : farT;
    }

    public float3 MediumExtinction(FAtmosphereParams params, float altitude)
    {
        const float rayleighDensity = exp(-max(altitude, 0.0f) / max(params.RayleighDensityH, 1.0e-3f));
        const float mieDensity = exp(-max(altitude, 0.0f) / max(params.MieDensityH, 1.0e-3f));
        const float ozoneDensity =
            saturate(1.0f - abs(altitude - params.OzoneCenterAltitude) / max(params.OzoneWidth, 1.0e-3f));
        return params.RayleighScattering * rayleighDensity +
               (params.MieScattering + params.MieAbsorption) * mieDensity +
               params.OzoneAbsorption * ozoneDensity;
    }

    public float3 IntegrateTransmittance(
        FAtmosphereParams params, float3 origin, float3 direction, float distance, uint stepCount)
    {
        const float stepLength = max(distance, 0.0f) / float(max(stepCount, 1u));
        float3 opticalDepth = float3(0.0f);
        for (uint i = 0; i < stepCount; ++i)
        {
            const float t = (float(i) + 0.5f) * stepLength;
            const float altitude = length(origin + direction * t) - params.BottomRadius;
            opticalDepth += MediumExtinction(params, altitude) * stepLength;
        }
        return exp(-opticalDepth);
    }

    public float RayleighPhase(float cosTheta)
    {
        return 3.0f * (1.0f + cosTheta * cosTheta) / (16.0f * 3.14159265359f);
    }

    public float MiePhase(float cosTheta, float g)
    {
        const float g2 = g * g;
        return (1.0f - g2) /
               (4.0f * 3.14159265359f *
                max(pow(1.0f + g2 - 2.0f * g * cosTheta, 1.5f), 1.0e-4f));
    }
}
#endif

#ifdef __cplusplus
#undef ATMOS_ALIGN_16
#endif

#endif
