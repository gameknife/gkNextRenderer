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
};

struct PushConstants
{
    uint64_t cameraAddress;
    uint64_t splatAddress;
    uint64_t paletteAddress;
    uint64_t sortedIndexAddress;
    uint64_t modelStateAddress;
    uint count;
    uint width;
    uint height;
    float sigma;
};

[[vk::push_constant]] PushConstants push;

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
    UniformBufferObject* camera = (UniformBufferObject*)push.cameraAddress;
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
    const float focalX = 0.5 * push.width * camera[0].Projection[0][0];
    const float focalY = 0.5 * push.height * abs(camera[0].Projection[1][1]);
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
    output.position = clip;
    output.local = local;
    output.colorOpacity = float4(EvaluateColor(splat, shDirection), alpha);
    return output;
}
