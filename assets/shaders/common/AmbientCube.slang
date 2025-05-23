implementing Common;
#include "PreProcessor.slang"

#define MAX_ILLUMINANCE 512.f

public static const int SHADOWMAP_SIZE = 4096;
public static const int CUBE_SIZE_XY = 192; // 256;
public static const int CUBE_SIZE_Z = 48;
public static const float CUBE_UNIT = 0.25f;
public static const float3 CUBE_OFFSET = float3(-CUBE_SIZE_XY / 2, -1.375f, -CUBE_SIZE_XY / 2) * CUBE_UNIT;
 
public static const float CUBE_UNIT_FAR = 4.0f; // cover 0.8km x 0.8km x 0.16km
public static const float3 CUBE_OFFSET_FAR = float3(-CUBE_SIZE_XY / 2, -1.375f, -CUBE_SIZE_XY / 2) * CUBE_UNIT_FAR;

public static const float3 cubeVectors[6] = {
    float3(0, 1, 0),
    float3(0, -1, 0),
    float3(0, 0, 1),
    float3(0, 0, -1),
    float3(1, 0, 0),
    float3(-1, 0, 0),
};

public static const uint FACE_TRACING = 16;

const static float2 grid4x4[16] = {
    float2(-0.75, -0.75), float2(-0.25, -0.75), float2(0.25, -0.75), float2(0.75, -0.75),
    float2(-0.75, -0.25), float2(-0.25, -0.25), float2(0.25, -0.25), float2(0.75, -0.25),
    float2(-0.75, 0.25), float2(-0.25, 0.25), float2(0.25, 0.25), float2(0.75, 0.25),
    float2(-0.75, 0.75), float2(-0.25, 0.75), float2(0.25, 0.75), float2(0.75, 0.75)
};

const static float2 grid5x5[25] = {
    float2(-0.8, -0.8), float2(-0.4, -0.8), float2(0.0, -0.8), float2(0.4, -0.8), float2(0.8, -0.8),
    float2(-0.8, -0.4), float2(-0.4, -0.4), float2(0.0, -0.4), float2(0.4, -0.4), float2(0.8, -0.4),
    float2(-0.8, 0.0), float2(-0.4, 0.0), float2(0.0, 0.0), float2(0.4, 0.0), float2(0.8, 0.0),
    float2(-0.8, 0.4), float2(-0.4, 0.4), float2(0.0, 0.4), float2(0.4, 0.4), float2(0.8, 0.4),
    float2(-0.8, 0.8), float2(-0.4, 0.8), float2(0.0, 0.8), float2(0.4, 0.8), float2(0.8, 0.8)
};

uint packRGB10A2(float4 color) {
    float4 clamped = clamp(color / MAX_ILLUMINANCE, float4(0.0f), float4(1.0f));

    uint r = uint(clamped.r * 1023.0f);
    uint g = uint(clamped.g * 1023.0f);
    uint b = uint(clamped.b * 1023.0f);
    uint a = uint(clamped.a * 3.0f);

    return r | (g << 10) | (b << 20) | (a << 30);
}

float4 unpackRGB10A2(uint packed) {
    float r = float((packed) & 0x3FF) / 1023.0f;
    float g = float((packed >> 10) & 0x3FF) / 1023.0f;
    float b = float((packed >> 20) & 0x3FF) / 1023.0f;
    // float a = packed & 0x3;

    return float4(r, g, b, 0.0) * MAX_ILLUMINANCE;
}

uint PackColor(float4 source) {
    return packRGB10A2(source);
}

float4 UnpackColor(uint packed) {
    return unpackRGB10A2(packed);
}

uint LerpPackedColor(uint c0, uint c1, float t) {
    // Extract RGBA components
    float4 color0 = UnpackColor(c0);
    float4 color1 = UnpackColor(c1);
    // Pack back to uint32
    return PackColor(lerp(color0, color1, t));
}

uint LerpPackedColorAlt(uint c0, float4 c1, float t) {
    // Extract RGBA components
    float4 color0 = UnpackColor(c0);
    float4 color1 = c1;
    // Pack back to uint32
    return PackColor(lerp(color0, color1, t));
}

float4 sampleAmbientCubeHL2_DI(AmbientCube cube, float3 normal) {
    float4 color = float4(0.0, 0.0, 0.0, 0.0);
    float sum = 0.0;

    float wx = max(normal.x, 0.0f);
    float wnx = max(-normal.x, 0.0f);
    float wy = max(normal.y, 0.0f);
    float wny = max(-normal.y, 0.0f);
    float wz = max(normal.z, 0.0f);
    float wnz = max(-normal.z, 0.0f);

    sum = wx + wnx + wy + wny + wz + wnz;

    color += wx * UnpackColor(cube.PosX_D);
    color += wnx * UnpackColor(cube.NegX_D);
    color += wy * UnpackColor(cube.PosY_D);
    color += wny * UnpackColor(cube.NegY_D);
    color += wz * UnpackColor(cube.PosZ_D);
    color += wnz * UnpackColor(cube.NegZ_D);

    color *= (sum > 0.0) ? (1.0 / sum) : 1.0;
    color.w = f16tof32(cube.Lighting & 0xFFFF);
    return color;
}

float4 sampleAmbientCubeHL2_Sky(AmbientCube cube, float3 normal) {
    float4 color = float4(0.0, 0.0, 0.0, 0.0);
    float sum = 0.0;

    float wx = max(normal.x, 0.0);
    float wnx = max(-normal.x, 0.0);
    float wy = max(normal.y, 0.0);
    float wny = max(-normal.y, 0.0);
    float wz = max(normal.z, 0.0);
    float wnz = max(-normal.z, 0.0);

    sum = wx + wnx + wy + wny + wz + wnz;

    color += wx * UnpackColor(cube.PosX_S);
    color += wnx * UnpackColor(cube.NegX_S);
    color += wy * UnpackColor(cube.PosY_S);
    color += wny * UnpackColor(cube.NegY_S);
    color += wz * UnpackColor(cube.PosZ_S);
    color += wnz * UnpackColor(cube.NegZ_S);

    color += wx * UnpackColor(cube.PosX);
    color += wnx * UnpackColor(cube.NegX);
    color += wy * UnpackColor(cube.PosY);
    color += wny * UnpackColor(cube.NegY);
    color += wz * UnpackColor(cube.PosZ);
    color += wnz * UnpackColor(cube.NegZ);

    // Normalize
    color.xyz *= (sum > 0.0) ? (1.0 / sum) : 1.0;
    color.w = f16tof32(cube.Lighting & 0xFFFF);
    return color;
}

float4 sampleAmbientCubeHL2_Full(AmbientCube cube, float3 normal) {
    float4 color = float4(0.0, 0.0, 0.0, 0.0);
    float sum = 0.0;

    float wx = max(normal.x, 0.0);
    float wnx = max(-normal.x, 0.0);
    float wy = max(normal.y, 0.0);
    float wny = max(-normal.y, 0.0);
    float wz = max(normal.z, 0.0);
    float wnz = max(-normal.z, 0.0);

    sum = wx + wnx + wy + wny + wz + wnz;

    color += wx * UnpackColor(cube.PosX_D);
    color += wnx * UnpackColor(cube.NegX_D);
    color += wy * UnpackColor(cube.PosY_D);
    color += wny * UnpackColor(cube.NegY_D);
    color += wz * UnpackColor(cube.PosZ_D);
    color += wnz * UnpackColor(cube.NegZ_D);

    color += wx * UnpackColor(cube.PosX);
    color += wnx * UnpackColor(cube.NegX);
    color += wy * UnpackColor(cube.PosY);
    color += wny * UnpackColor(cube.NegY);
    color += wz * UnpackColor(cube.PosZ);
    color += wnz * UnpackColor(cube.NegZ);

    // Normalize
    color.xyz *= (sum > 0.0) ? (1.0 / sum) : 1.0;
    color.w = f16tof32(cube.Lighting & 0xFFFF);
    return color;
}

interface IAmbientCubeSampler
{
    static float4 Sample(AmbientCube cube, float3 normal);
}

struct SkyAmbientCubeSampler : IAmbientCubeSampler
{
    static float4 Sample(AmbientCube cube, float3 normal)
    {
        return sampleAmbientCubeHL2_Sky(cube, normal);
    }
};

struct FullAmbientCubeSampler : IAmbientCubeSampler
{
    static float4 Sample(AmbientCube cube, float3 normal)
    {
        return sampleAmbientCubeHL2_Full(cube, normal);
    }
};

struct DIAmbientCubeSampler : IAmbientCubeSampler
{
    static float4 Sample(AmbientCube cube, float3 normal)
    {
        return sampleAmbientCubeHL2_DI(cube, normal);
    }
};

AmbientCube FetchCube(int3 probePos, bool useFar, in RWStructuredBuffer<AmbientCube> Cubes, in RWStructuredBuffer<AmbientCube> FarCubes)
{
    int idx = probePos.y * CUBE_SIZE_XY * CUBE_SIZE_XY +
              probePos.z * CUBE_SIZE_XY + probePos.x;
    return useFar ? FarCubes[idx] : Cubes[idx];
}

public float4 interpolateAmbientCubes<T:IAmbientCubeSampler>(float3 pos, float3 normal, in RWStructuredBuffer<AmbientCube> Cubes, in RWStructuredBuffer<AmbientCube> FarCubes)
{
    bool useFar = false;

    float3 nearpos = (pos - CUBE_OFFSET) / CUBE_UNIT;
    float3 farpos = (pos - CUBE_OFFSET_FAR) / CUBE_UNIT_FAR;

    pos = nearpos;

    // Early out if position is outside the probe grid
    if (nearpos.x < 0 || nearpos.y < 0 || nearpos.z < 0 ||
        nearpos.x > CUBE_SIZE_XY - 1 || nearpos.y > CUBE_SIZE_Z - 1 || nearpos.z > CUBE_SIZE_XY - 1) {
        useFar = true;
        pos = farpos;

        if (farpos.x < 0 || farpos.y < 0 || farpos.z < 0 ||
            farpos.x > CUBE_SIZE_XY - 1 || farpos.y > CUBE_SIZE_Z - 1 || farpos.z > CUBE_SIZE_XY - 1) {
            return float4(0, 0, 0, 0);
        }
    }

    // Get the base indices and fractional positions
    int3 baseIdx = int3(floor(pos));
    float3 frac = frac(pos);

    // Trilinear interpolation between 8 nearest probes
    // Try immediate neighbors first
    float totalWeight = 0.0;
    float4 result = float4(0.0, 0.0, 0.0, 0.0);

    for (int i = 0; i < 8; i++) {
        int3 offset = int3(
            i & 1,
            (i >> 1) & 1,
            (i >> 2) & 1
        );

        int3 probePos = baseIdx + offset;
        AmbientCube cube = FetchCube(probePos, useFar, Cubes, FarCubes);
        if (cube.Active == 0 && cube.ExtInfo3 == 0) continue;

        float wx = offset.x == 0 ? (1.0 - frac.x) : frac.x;
        float wy = offset.y == 0 ? (1.0 - frac.y) : frac.y;
        float wz = offset.z == 0 ? (1.0 - frac.z) : frac.z;
        float weight = wx * wy * wz;

        float occlusion;
        float4 sampleColor = T.Sample(cube, normal);
        result += sampleColor * weight;
        totalWeight += weight;
    }

    // Normalize result by total weight
    float4 indirectColor = totalWeight > 0.0 ? result / totalWeight : float4(0.05, 0.05, 0.05, 0.05);
    return indirectColor;
}

// Interpolate between 8 probes
public bool inSolid(float3 pos, inout uint matId, in RWStructuredBuffer<AmbientCube> Cubes, in RWStructuredBuffer<AmbientCube> FarCubes) {
    // Early out if position is outside the probe grid
    if (pos.x <= 0 || pos.y <= 0 || pos.z <= 0 ||
        pos.x > CUBE_SIZE_XY - 1 || pos.y > CUBE_SIZE_Z - 1 || pos.z > CUBE_SIZE_XY - 1) {
        return false;
    }

    int3 baseIdx = int3(floor(pos));

    AmbientCube cube = FetchCube(baseIdx, false, Cubes, FarCubes);
    matId = cube.ExtInfo1;
    return cube.Active == 1 ? false : true;
}

public interface IProbeGenerator
{
    void Render(IRayTracer tracer, float3 origin, uint CubeIdx, inout uint4 RandomSeed);
}

public struct FGpuProbeGenerator : IProbeGenerator
{
    LightObject FetchLight(uint lightIdx, inout float4 lightPower)
    {
        lightPower = Materials[Lights[lightIdx].lightMatIdx].Diffuse;
        return Lights[lightIdx];
    }

    bool FaceTask(IRayTracer tracer, float3 origin, float3 basis, uint iterate, inout uint DirectLight, inout uint IndirectLight, inout uint SkyLight)
    {
        float4 rayColor = float4(0.0);
        float4 bounceColor = float4(0.0);
        float4 skyColor = float4(0.0);

        const float2 offset = grid5x5[iterate % 25] * 0.25f;
        float skyMultiplier = Camera.HasSky ? Camera.SkyIntensity : 0.0f;

        bool offsetProbe = false;
        float3 probeOffset = float3(0.0);

        for (uint i = 0; i < FACE_TRACING; i++)
        {
            float3 hemiVec = float3(grid4x4[i] + offset, 1.0);

            // Align with the surface normal
            float3 rayDir = AlignWithNormal(hemiVec, basis);

            float3 OutNormal;
            Material OutMaterial;
            uint OutMaterialId;
            uint OutInstanceId;
            float OutRayDist;

            if (tracer.TraceRay(origin, rayDir, OutNormal, OutRayDist, OutNormal, OutMaterial, OutMaterialId))
            {
                float3 hitPos = origin + rayDir * OutRayDist;

                if (dot(OutNormal, rayDir) < 0.0)
                {
                    bounceColor += OutMaterial.Diffuse * interpolateAmbientCubes<DIAmbientCubeSampler>(hitPos, OutNormal, Cubes, Cubes);
                }
                else
                {
                    return true;
                }
            }
            else
            {
                rayColor += Common.SampleIBLRough(Camera.SkyIdx, rayDir, Camera.SkyRotation, HDRSHs ) * skyMultiplier;
            }
        }
        rayColor = rayColor / float(FACE_TRACING);
        skyColor = rayColor;
        bounceColor = bounceColor / float(FACE_TRACING);

        if (Camera.LightCount > 0)
        {
            float4 lightPower = float4(0.0);
            LightObject light = FetchLight(0, lightPower);
            float3 lightPos = lerp(light.p1.xyz, light.p3.xyz, 0.5f);
            float lightAtten = float(tracer.TraceSegment(origin, lightPos));
            float3 lightDir = normalize(lightPos - origin);
            float ndotl = clamp(dot(basis, lightDir), 0.0f, 1.0f);
            float distance = length(lightPos - origin);
            float attenuation = ndotl * light.normal_area.w / (distance * distance * 3.14159f);
            rayColor += lightPower * attenuation * lightAtten;
        }

        float3 sunDir = Camera.SunDirection.xyz;
        float sunAtten = float(tracer.TraceOcclusion(origin, sunDir));
        float ndotl = clamp(dot(basis, sunDir), 0.0f, 1.0f);
        rayColor += Camera.SunColor * sunAtten * ndotl * (Camera.HasSun ? 1.0f : 0.0f) * 0.25f;

        DirectLight = LerpPackedColorAlt(DirectLight, rayColor, 0.25);
        IndirectLight = LerpPackedColorAlt(IndirectLight, bounceColor, 0.25);
        SkyLight = LerpPackedColorAlt(SkyLight, skyColor, 0.25);

        return false;
    }

    bool InsideGeometry(IRayTracer tracer, float3 origin, inout float3 offset, out uint OutMaterialId)
    {
        for (uint i = 0; i < 6; i++)
        {
            float3 rayDir = cubeVectors[i];

            // Align with the surface normal
            float3 OutNormal;
            Material OutMaterial;
            uint OutInstanceId;
            float OutRayDist;

            if (tracer.TraceRay(origin, rayDir, OutNormal, OutRayDist, OutNormal, OutMaterial, OutMaterialId))
            {
                if (dot(OutNormal, rayDir) > 0.0)
                {
                    offset = rayDir * (OutRayDist + 0.05f);
                    if (OutRayDist < CUBE_UNIT)
                    {
                        return true;
                    }
                }
            }
        }
        OutMaterialId = 0;
        return false;
    }

    public void Render(IRayTracer tracer, float3 origin, uint CubeIdx, inout uint4 RandomSeed)
    {
        // target
        // Cubes[CubeIdx];
        float3 offset = float3(0, 0, 0);
        uint iterate = Cubes[CubeIdx].ExtInfo2;
        Cubes[CubeIdx].ExtInfo2 = Cubes[CubeIdx].ExtInfo2 + 1;
        bool Solid = false;

        if (InsideGeometry(tracer, origin, offset, Cubes[CubeIdx].ExtInfo1))
        {
            // INSIDE
            origin += offset;
            Solid = true;
        }

        Solid = Solid || FaceTask(tracer, origin, float3(0, 1, 0), iterate, Cubes[CubeIdx].PosY_D, Cubes[CubeIdx].PosY, Cubes[CubeIdx].PosY_S);
        Solid = Solid || FaceTask(tracer, origin, float3(0, -1, 0), iterate, Cubes[CubeIdx].NegY_D, Cubes[CubeIdx].NegY, Cubes[CubeIdx].NegY_S);
        Solid = Solid || FaceTask(tracer, origin, float3(1, 0, 0), iterate, Cubes[CubeIdx].PosX_D, Cubes[CubeIdx].PosX, Cubes[CubeIdx].PosX_S);
        Solid = Solid || FaceTask(tracer, origin, float3(-1, 0, 0), iterate, Cubes[CubeIdx].NegX_D, Cubes[CubeIdx].NegX, Cubes[CubeIdx].NegX_S);
        Solid = Solid || FaceTask(tracer, origin, float3(0, 0, 1), iterate, Cubes[CubeIdx].PosZ_D, Cubes[CubeIdx].PosZ, Cubes[CubeIdx].PosZ_S);
        Solid = Solid || FaceTask(tracer, origin, float3(0, 0, -1), iterate, Cubes[CubeIdx].NegZ_D, Cubes[CubeIdx].NegZ, Cubes[CubeIdx].NegZ_S);

        Cubes[CubeIdx].Active = Solid ? 0 : 1;
        Cubes[CubeIdx].ExtInfo3 = Cubes[CubeIdx].Active;
    }

    ConstantBuffer<UniformBufferObject> Camera;
    StructuredBuffer<float> Vertices;
    StructuredBuffer<uint> Indices;
    StructuredBuffer<Material> Materials;
    StructuredBuffer<uint2> Offsets;
    StructuredBuffer<NodeProxy> NodeProxies;

    RWStructuredBuffer<AmbientCube> Cubes;
    StructuredBuffer<SphericalHarmonics> HDRSHs;
    StructuredBuffer<LightObject> Lights;
}