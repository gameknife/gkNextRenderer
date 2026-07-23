import Shader.Bindless;
import Shader.Types;

struct GaussianSplat
{
    float4 positionOpacity;
    float4 covariance0;
    float4 covariance1;
    float4 sh0;
    uint4 metadata;
};

struct SplatModelState
{
    float4x4 world;
    float4 parameters;
    float4 lightingParameters;
};

struct PushConstants
{
    GPUScene gpuScene;
    uint64_t splatAddress;
    uint64_t paletteAddress;
    uint64_t sortedIndexAddress;
    uint64_t modelStateAddress;
    uint64_t lightingGridAddress;
    uint count;
    uint width;
    uint height;
    float sigma;
};

[[vk::push_constant]] PushConstants push;

namespace Bindless
{
    public GPUScene GetGpuscene()
    {
        return push.gpuScene;
    }
}

#include "common/ConstFunc.slang"

static const int SPLAT_CUBE_SIZE_XY = 192;
static const int SPLAT_CUBE_SIZE_Z = 48;
static const uint SPLAT_CUBE_CASCADE_MAX = 4;

struct VertexOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float2 local : LOCAL;
    [[vk::location(1)]] nointerpolation float4 colorOpacity : COLOR_OPACITY;
};

float3 LocalCovarianceMul(GaussianSplat splat, float3 value)
{
    return float3(
        splat.covariance0.x * value.x + splat.covariance0.y * value.y + splat.covariance0.z * value.z,
        splat.covariance0.y * value.x + splat.covariance0.w * value.y + splat.covariance1.x * value.z,
        splat.covariance0.z * value.x + splat.covariance1.x * value.y + splat.covariance1.y * value.z);
}

float3 CovarianceMul(GaussianSplat splat, SplatModelState model, float3 value)
{
    const float3x3 linear = (float3x3)model.world;
    const float3 localValue = mul(transpose(linear), value);
    return mul(linear, LocalCovarianceMul(splat, localValue));
}

float3 EvaluateColor(GaussianSplat splat, float3 direction)
{
    const float c0 = 0.28209479177387814;
    float3 color = splat.sh0.xyz * c0;
    const uint bands = splat.metadata.y;
    const uint coefficientCount = bands == 1 ? 3 : (bands == 2 ? 8 : (bands == 3 ? 15 : 0));
    if (coefficientCount > 0)
    {
        float4* coefficients = (float4*)push.paletteAddress + splat.metadata.z +
                               splat.metadata.x * coefficientCount;
        const float x = direction.x;
        const float y = direction.y;
        const float z = direction.z;
        float basis[15];
        basis[0] = -0.4886025119029199 * y;
        basis[1] =  0.4886025119029199 * z;
        basis[2] = -0.4886025119029199 * x;
        basis[3] =  1.0925484305920792 * x * y;
        basis[4] = -1.0925484305920792 * y * z;
        basis[5] =  0.31539156525252005 * (2.0 * z * z - x * x - y * y);
        basis[6] = -1.0925484305920792 * x * z;
        basis[7] =  0.5462742152960396 * (x * x - y * y);
        basis[8] = -0.5900435899266435 * y * (3.0 * x * x - y * y);
        basis[9] =  2.890611442640554 * x * y * z;
        basis[10] = -0.4570457994644658 * y * (4.0 * z * z - x * x - y * y);
        basis[11] = 0.3731763325901154 * z * (2.0 * z * z - 3.0 * x * x - 3.0 * y * y);
        basis[12] = -0.4570457994644658 * x * (4.0 * z * z - x * x - y * y);
        basis[13] = 1.445305721320277 * z * (x * x - y * y);
        basis[14] = -0.5900435899266435 * x * (x * x - 3.0 * y * y);
        for (uint coefficient = 0; coefficient < coefficientCount; ++coefficient)
        {
            color += coefficients[coefficient].xyz * basis[coefficient];
        }
    }
    return max(color + 0.5, 0.0);
}

uint SelectSunCascade(float viewDist, float4 splits)
{
    if (viewDist < splits.x) return 0;
    if (viewDist < splits.y) return 1;
    if (viewDist < splits.z) return 2;
    return 3;
}

float SampleSplatSunShadowCSM(UniformBufferObject camera, float3 worldPos, float3 normal, float viewDist)
{
    const uint cascade = SelectSunCascade(viewDist, camera.CascadeSplits);
    const float4 lightPos = mul(camera.SunCascadeViewProjection[cascade], float4(worldPos, 1.0));
    const float3 projCoords = lightPos.xyz / lightPos.w;
    const float2 uv = projCoords.xy * 0.5 + 0.5;
    if (any(uv < float2(0.0)) || any(uv > float2(1.0)) || projCoords.z < 0.0 || projCoords.z > 1.0)
    {
        return 1.0;
    }

    const float noL = max(dot(normalize(normal), camera.SunDirection.xyz), 0.0);
    const float bias = max(0.0008 * (1.0 - noL), 0.0001);
    Sampler2D shadowMap = Bindless.GetShadowMap(int(cascade));
    uint2 texSize;
    shadowMap.GetDimensions(texSize.x, texSize.y);
    const float2 texelSize = 1.0 / float2(texSize);

    float shadow = 0.0;
    [unroll]
    for (int dy = -1; dy <= 1; ++dy)
    {
        [unroll]
        for (int dx = -1; dx <= 1; ++dx)
        {
            const float2 offset = float2(dx, dy) * texelSize;
            const float closestDepth = shadowMap.SampleLevel(uv + offset, 0).r;
            shadow += (projCoords.z - bias) > closestDepth ? 0.0 : 1.0;
        }
    }
    return shadow * (1.0 / 9.0);
}

uint GetSplatAmbientCubeCascadeCount()
{
    uint count = uint(max(Bindless.GetGpuscene().Camera[0].AmbientCubeCascadeParams.x, 1.0));
    return min(count, SPLAT_CUBE_CASCADE_MAX);
}

float GetSplatAmbientCubeUnitAtCascade(uint cascadeIndex)
{
    const UniformBufferObject camera = Bindless.GetGpuscene().Camera[0];
    const float baseUnit = max(camera.AmbientCubeUnit, 0.0001);
    const float ratio = max(camera.AmbientCubeCascadeParams.y, 1.0);
    return baseUnit * pow(ratio, float(cascadeIndex));
}

float3 GetSplatAmbientCubeOffsetBias()
{
    const UniformBufferObject camera = Bindless.GetGpuscene().Camera[0];
    const float3 baseOffset = float3(-SPLAT_CUBE_SIZE_XY / 2.0, -1.375, -SPLAT_CUBE_SIZE_XY / 2.0) *
                              max(camera.AmbientCubeUnit, 0.0001);
    return camera.AmbientCubeOffset - baseOffset;
}

float3 GetSplatAmbientCubeOffsetAtCascade(uint cascadeIndex)
{
    const float unit = GetSplatAmbientCubeUnitAtCascade(cascadeIndex);
    return float3(-SPLAT_CUBE_SIZE_XY / 2.0, -1.375, -SPLAT_CUBE_SIZE_XY / 2.0) * unit +
           GetSplatAmbientCubeOffsetBias();
}

uint SplatAmbientBrickSlot(uint cascadeIndex, int3 probePos)
{
    if (probePos.x < 0 || probePos.y < 0 || probePos.z < 0 ||
        probePos.x >= SPLAT_CUBE_SIZE_XY || probePos.y >= SPLAT_CUBE_SIZE_Z || probePos.z >= SPLAT_CUBE_SIZE_XY)
    {
        return GPU_SCENE_AMBIENT_BRICK_INVALID;
    }
    const uint bx = uint(probePos.x / GPU_SCENE_AMBIENT_BRICK_EDGE);
    const uint by = uint(probePos.y / GPU_SCENE_AMBIENT_BRICK_EDGE);
    const uint bz = uint(probePos.z / GPU_SCENE_AMBIENT_BRICK_EDGE);
    const uint brickLinear = by * uint(GPU_SCENE_AMBIENT_BRICKS_X * GPU_SCENE_AMBIENT_BRICKS_Z) +
                             bz * uint(GPU_SCENE_AMBIENT_BRICKS_X) + bx;
    return Bindless.GetGpuscene().AmbientBrickTable[
        cascadeIndex * uint(GPU_SCENE_AMBIENT_BRICKS_PER_CASCADE) + brickLinear];
}

uint SplatAmbientBrickLocalOffset(int3 probePos)
{
    const uint lx = uint(probePos.x & int(GPU_SCENE_AMBIENT_BRICK_EDGE - 1));
    const uint ly = uint(probePos.y & int(GPU_SCENE_AMBIENT_BRICK_EDGE - 1));
    const uint lz = uint(probePos.z & int(GPU_SCENE_AMBIENT_BRICK_EDGE - 1));
    return ly * uint(GPU_SCENE_AMBIENT_BRICK_EDGE * GPU_SCENE_AMBIENT_BRICK_EDGE) +
           lz * uint(GPU_SCENE_AMBIENT_BRICK_EDGE) + lx;
}

int SplatAmbientCubePoolIndex(uint cascadeIndex, int3 probePos)
{
    const uint slot = SplatAmbientBrickSlot(cascadeIndex, probePos);
    if (slot == GPU_SCENE_AMBIENT_BRICK_INVALID)
    {
        return -1;
    }
    const uint poolBricks = Bindless.GetGpuscene().AmbientPoolBricksPerCascade;
    return int((cascadeIndex * poolBricks + slot) * uint(GPU_SCENE_AMBIENT_BRICK_VOLUME) +
               SplatAmbientBrickLocalOffset(probePos));
}

AmbientCube SplatZeroAmbientCube()
{
    AmbientCube cube;
    cube.PosZ = 0;
    cube.NegZ = 0;
    cube.PosY = 0;
    cube.NegY = 0;
    cube.PosX = 0;
    cube.NegX = 0;
    cube.SunDirect = 0;
    cube.SkyEmissiveDirect = 0;
    cube.SkyVisibilityPZNZPYNY = 0;
    cube.SkyVisibilityPXNXS0S1 = 0;
    return cube;
}

bool SplatTryFetchCubePool(int3 probePos, uint cascadeIndex, out AmbientCube cube)
{
    const int idx = SplatAmbientCubePoolIndex(cascadeIndex, probePos);
    if (idx < 0)
    {
        cube = SplatZeroAmbientCube();
        return false;
    }
    cube = Bindless.GetGpuscene().Cubes[idx];
    return true;
}

float SplatAmbientCubeSkyVisibility(AmbientCube cube, float3 normal)
{
    const float wx = max(normal.x, 0.0);
    const float wnx = max(-normal.x, 0.0);
    const float wy = max(normal.y, 0.0);
    const float wny = max(-normal.y, 0.0);
    const float wz = max(normal.z, 0.0);
    const float wnz = max(-normal.z, 0.0);
    const float sum = wx + wnx + wy + wny + wz + wnz;

    const uint4 unpacked0 = UnpackBytes(cube.SkyVisibilityPZNZPYNY);
    const uint4 unpacked1 = UnpackBytes(cube.SkyVisibilityPXNXS0S1);
    float skyvis = 0.0;
    skyvis += float(unpacked0.x) * wz;
    skyvis += float(unpacked0.y) * wnz;
    skyvis += float(unpacked0.z) * wy;
    skyvis += float(unpacked0.w) * wny;
    skyvis += float(unpacked1.x) * wx;
    skyvis += float(unpacked1.y) * wnx;
    skyvis *= (sum > 0.0) ? (1.0 / sum) : 1.0;
    return skyvis / 255.0;
}

float SampleSplatUpSkyVisibility(float3 worldPos)
{
    const float3 upNormal = float3(0.0, 1.0, 0.0);
    const uint cascadeCount = GetSplatAmbientCubeCascadeCount();
    for (uint cascadeIndex = 0; cascadeIndex < cascadeCount; ++cascadeIndex)
    {
        const float cubeUnit = GetSplatAmbientCubeUnitAtCascade(cascadeIndex);
        const float3 cubeOffset = GetSplatAmbientCubeOffsetAtCascade(cascadeIndex);
        const float3 nearPos = (worldPos - cubeOffset) / cubeUnit;
        if (nearPos.x < 0.0 || nearPos.y < 0.0 || nearPos.z < 0.0 ||
            nearPos.x > SPLAT_CUBE_SIZE_XY - 2 || nearPos.y > SPLAT_CUBE_SIZE_Z - 2 ||
            nearPos.z > SPLAT_CUBE_SIZE_XY - 2)
        {
            continue;
        }

        const int3 baseIdx = int3(floor(nearPos));
        const float3 fracPos = frac(nearPos);
        float totalWeight = 0.0;
        float skyVisibility = 0.0;
        for (int i = 0; i < 8; ++i)
        {
            const int3 offset = int3(i & 1, (i >> 1) & 1, (i >> 2) & 1);
            const int3 probePos = baseIdx + offset;
            AmbientCube cube;
            if (!SplatTryFetchCubePool(probePos, cascadeIndex, cube))
            {
                continue;
            }

            const float wx = offset.x == 0 ? (1.0 - fracPos.x) : fracPos.x;
            const float wy = offset.y == 0 ? (1.0 - fracPos.y) : fracPos.y;
            const float wz = offset.z == 0 ? (1.0 - fracPos.z) : fracPos.z;
            const float weight = wx * wy * wz;
            skyVisibility += SplatAmbientCubeSkyVisibility(cube, upNormal) * weight;
            totalWeight += weight;
        }
        if (totalWeight > 0.0)
        {
            return saturate(skyVisibility / totalWeight);
        }
    }
    return 1.0;
}

float3 ApplySceneLighting(GaussianSplat splat, SplatModelState model, UniformBufferObject camera,
                          float3 baseColor, float3 worldPosition, float3 eye, float viewDist)
{
    const float strength = saturate(model.lightingParameters.y);
    if (model.lightingParameters.x < 0.5 || strength <= 0.0)
    {
        return baseColor;
    }

    float3 worldUp = normalize(mul((float3x3)model.world, float3(0.0, 1.0, 0.0)));
    if (length(worldUp) < 0.5)
    {
        worldUp = float3(0.0, 1.0, 0.0);
    }

    float shadowMask = 1.0;
    if (camera.HasSun)
    {
        shadowMask = SampleSplatSunShadowCSM(camera, worldPosition + worldUp * 0.015, worldUp, viewDist);
    }

    const float upwardSkyVisibility = SampleSplatUpSkyVisibility(worldPosition);
    const float skyMask = lerp(0.05, 1.0, pow(upwardSkyVisibility + 0.5, 2.0));
    const float sunShadowMask = lerp(0.05, 1.0, shadowMask);
    const float lightingMask = saturate(skyMask * sunShadowMask);
    const bool hasSh = splat.metadata.y > 0u || any(abs(splat.sh0.xyz) > float3(1e-5));
    const float3 litColor = hasSh ? baseColor * lightingMask : float3(lightingMask);
    return lerp(baseColor, litColor, strength);
}

[shader("vertex")]
VertexOutput main(uint vertexIndex : SV_VertexID, uint instanceIndex : SV_InstanceID)
{
    VertexOutput output;
    const float2 corners[6] = {
        float2(-1.0, -1.0), float2(1.0, -1.0), float2(1.0, 1.0),
        float2(-1.0, -1.0), float2(1.0, 1.0), float2(-1.0, 1.0)
    };
    uint* sortedIndices = (uint*)push.sortedIndexAddress;
    GaussianSplat* splats = (GaussianSplat*)push.splatAddress;
    const GaussianSplat splat = splats[sortedIndices[instanceIndex]];
    SplatModelState* modelStates = (SplatModelState*)push.modelStateAddress;
    const SplatModelState model = modelStates[splat.metadata.w];
    UniformBufferObject* camera = push.gpuScene.Camera;
    const float3 worldPosition = mul(model.world, float4(splat.positionOpacity.xyz, 1.0)).xyz;
    const float4 viewPosition = mul(camera[0].ModelView, float4(worldPosition, 1.0));
    float4 clip = mul(camera[0].Projection, viewPosition);
    clip.z = clamp(clip.z, 0.0, abs(clip.w));

    float3 right = float3(camera[0].ModelView[0][0], camera[0].ModelView[0][1], camera[0].ModelView[0][2]);
    float3 up = float3(camera[0].ModelView[1][0], camera[0].ModelView[1][1], camera[0].ModelView[1][2]);
    float3 forward = float3(camera[0].ModelView[2][0], camera[0].ModelView[2][1], camera[0].ModelView[2][2]);
    const float c00 = dot(right, CovarianceMul(splat, model, right));
    const float c01 = dot(right, CovarianceMul(splat, model, up));
    const float c02 = dot(right, CovarianceMul(splat, model, forward));
    const float c11 = dot(up, CovarianceMul(splat, model, up));
    const float c12 = dot(up, CovarianceMul(splat, model, forward));
    const float c22 = dot(forward, CovarianceMul(splat, model, forward));

    const float depth = max(-viewPosition.z, 1e-4);
    const float focalX = 0.5 * push.width * camera[0].ProjectionUnJit[0][0];
    const float focalY = 0.5 * push.height * abs(camera[0].ProjectionUnJit[1][1]);
    const float3 jacobianX = float3(focalX / depth, 0.0, focalX * viewPosition.x / (depth * depth));
    const float3 jacobianY = float3(0.0, focalY / depth, focalY * viewPosition.y / (depth * depth));
    const float3x3 viewCovariance = float3x3(c00, c01, c02, c01, c11, c12, c02, c12, c22);
    const float covXX = dot(jacobianX, mul(viewCovariance, jacobianX));
    const float covXY = dot(jacobianX, mul(viewCovariance, jacobianY));
    const float covYY = dot(jacobianY, mul(viewCovariance, jacobianY));
    const float detOriginal = covXX * covYY - covXY * covXY;
    const float detBlur = (covXX + 0.3) * (covYY + 0.3) - covXY * covXY;
    const float aaFactor = lerp(1.0, sqrt(max(detOriginal / max(detBlur, 1e-8), 0.0)),
                                saturate(model.parameters.z));
    const float a = max(covXX + 0.3, 0.3);
    const float b = covXY;
    const float d = max(covYY + 0.3, 0.3);
    const float mid = 0.5 * (a + d);
    const float delta = sqrt(max(0.0, 0.25 * (a - d) * (a - d) + b * b));
    const float lambda0 = max(mid + delta, 0.3);
    const float lambda1 = max(mid - delta, 0.1);
    const float radiusScale = push.sigma / 3.0;
    const float viewportMin = min(1024.0, min((float)push.width, (float)push.height));
    const float radius0 = 2.0 * min(sqrt(2.0 * lambda0), viewportMin) * radiusScale;
    const float radius1 = 2.0 * min(sqrt(2.0 * lambda1), viewportMin) * radiusScale;
    if (max(radius0, radius1) < 2.0)
    {
        clip = float4(2.0, 2.0, 2.0, 1.0);
    }
    const float maxRadius = max(radius0, radius1);
    const float2 clipRadius = maxRadius * float2(2.0 / push.width, 2.0 / push.height) * clip.w;
    if (any((abs(clip.xy) - clipRadius) > clip.w))
    {
        clip = float4(2.0, 2.0, 2.0, 1.0);
    }
    float2 axis0 = abs(b) > 1e-6 ? normalize(float2(b, lambda0 - a)) :
                   (a >= d ? float2(1.0, 0.0) : float2(0.0, 1.0));
    const float alpha = splat.positionOpacity.w * model.parameters.x * aaFactor;
    const float alphaClip = 1.0 / 255.0;
    if (alpha <= alphaClip)
    {
        clip = float4(2.0, 2.0, 2.0, 1.0);
    }
    const float clipCorner = min(1.0, 0.5 * sqrt(max(0.0, log(alpha / alphaClip))));
    const float2 axis1 = float2(-axis0.y, axis0.x);
    const float2 local = corners[vertexIndex] * clipCorner;
    const float2 pixelOffset = axis0 * radius0 * local.x + axis1 * radius1 * local.y;
    clip.xy += pixelOffset * float2(2.0 / push.width, 2.0 / push.height) * clip.w;
    if (viewPosition.z >= -1e-4 || clip.w <= 0.0)
    {
        clip = float4(2.0, 2.0, 2.0, 1.0);
    }

    const float3 eye = mul(camera[0].ModelViewInverse, float4(0.0, 0.0, 0.0, 1.0)).xyz;
    const float3 worldDirection = normalize(worldPosition - eye);
    float3 shDirection = normalize(mul(transpose((float3x3)model.world), worldDirection));
    if (model.parameters.w > 0.5)
    {
        shDirection.xy = -shDirection.xy;
    }
    const float3 baseColor = EvaluateColor(splat, shDirection);
    const float3 litColor = ApplySceneLighting(splat, model, camera[0], baseColor, worldPosition, eye, depth);
    output.position = clip;
    output.local = local;
    output.colorOpacity = float4(litColor, alpha);
    return output;
}
