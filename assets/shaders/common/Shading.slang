implementing Common;
#include "PreProcessor.slang"

namespace Common
{
    float3 EvaluateSH(float SHCoefficients[3][9], float3 normal, float rotate) {
        // Apply rotation around Y-axis (0 to 2 maps to 0 to 360 degrees)
        float angle = rotate * 3.14159265358979323846f;
        float cosAngle = cos(angle);
        float sinAngle = sin(angle);
        
        // Rotate the normal vector around Y-axis
        float3 rotatedNormal = float3(
            normal.x * cosAngle + normal.z * sinAngle,
            normal.y,
            -normal.x * sinAngle + normal.z * cosAngle
        );
        
        // SH basis function evaluation
        static const float SH_C0 = 0.282095f;
        static const float SH_C1 = 0.488603f;
        static const float SH_C2 = 1.092548f;
        static const float SH_C3 = 0.315392f;
        static const float SH_C4 = 0.546274f;
        
        float basis[9];
        basis[0] = SH_C0;
        basis[1] = -SH_C1 * rotatedNormal.y;
        basis[2] = SH_C1 * rotatedNormal.z;
        basis[3] = -SH_C1 * rotatedNormal.x;
        basis[4] = SH_C2 * rotatedNormal.x * rotatedNormal.y;
        basis[5] = -SH_C2 * rotatedNormal.y * rotatedNormal.z;
        basis[6] = SH_C3 * (3.f * rotatedNormal.y * rotatedNormal.y - 1.0f);
        basis[7] = -SH_C2 * rotatedNormal.x * rotatedNormal.z;
        basis[8] = SH_C4 * (rotatedNormal.x * rotatedNormal.x - rotatedNormal.z * rotatedNormal.z);
        
        float3 color = float3(0.0, 0.0, 0.0);
        for (int i = 0; i < 9; ++i) {
            color.r += SHCoefficients[0][i] * basis[i];
            color.g += SHCoefficients[1][i] * basis[i];
            color.b += SHCoefficients[2][i] * basis[i];
        }
        
        return color;
    };

    public float4 SampleIBL(uint skyIdx, float3 direction, float rotate, float roughness, in StructuredBuffer<SphericalHarmonics> HDRSHs, in Sampler2D[] TextureSamplers)
    {
         if(roughness > 0.6f)
         {
               float3 rayColor = EvaluateSH(HDRSHs[skyIdx].coefficients, direction, 1.0 - rotate);
               return float4(rayColor * 1.0f, 1.0);
         }
         float3 d = normalize(direction);
         float2 t = float2((atan2(d.x, d.z) + M_PI * rotate) * M_1_OVER_TWO_PI, acos(d.y) * M_1_PI);
         // Sample with explicit LOD based on roughness
         return min(float4(10,10,10,1), TextureSamplers[NonUniformResourceIndex(skyIdx)].SampleLevel(t, roughness * 10.0f));
    };

    // 获取阴影值
    public float getShadow(float3 worldPos, float3 jit, float3 normal, int2 ipos, in UniformBufferObject ubo, in Sampler2D shadowmap) {
        // 计算光源空间坐标
        float4 posInLightMap = mul(ubo.SunViewProjection, float4(worldPos + jit * 4.0f, 1.0f));
    
        // 将光源空间坐标转换到NDC空间 [-1,1] 再转到 [0,1] 的纹理空间
        float3 projCoords = posInLightMap.xyz / posInLightMap.w;
        projCoords = projCoords * 0.5 + 0.5;
        projCoords.y = 1.0 - projCoords.y;
    
        float currentDepth = projCoords.z;
    
        float bias = 0.0005;
    
        float cosTheta = max(dot(normal, normalize(ubo.SunDirection.xyz)), 0.0);
        bias = lerp(0.0001, 0.00005, cosTheta);
    
        float closestDepth = shadowmap.Sample(projCoords.xy).x;
        float shadow = currentDepth - bias > closestDepth ? 0.0 : 1.0;
    
        return shadow;
    };

    bool traceLongDistance(float3 position, float3 rayDir, float maxDistance, float depthTolerance, out float3 outPosition, out float3 outNormal, out uint outMaterialIdx,
                           in ConstantBuffer<UniformBufferObject> Camera, in RWTexture2D<uint2> MiniGBuffer, in StructuredBuffer<NodeProxy> NodeProxies, in StructuredBuffer<float> Vertices,
                           in RWStructuredBuffer<AmbientCube> Cubes, in RWStructuredBuffer<AmbientCube> FarCubes)
    {
        // Near field tracing parameters
        const float nearDistance = 0.25;            // Near field tracing distance
        const float longDistanceInit = 0.5;         // Far field starting distance (to avoid self-intersection)
        const float stepSize = 0.25;                // Far field step size
        const float maxTotalDistance = maxDistance; // Maximum total tracing distance

        if (traceInScreenSpace(position, rayDir, nearDistance, depthTolerance, outPosition, outNormal, outMaterialIdx, Camera, MiniGBuffer, NodeProxies, Vertices)) {
            return true;
        }

        float tracedDistance = longDistanceInit;
        float3 currentPos = position + rayDir * longDistanceInit;

        while (tracedDistance < maxTotalDistance) {
            float3 probePos = (currentPos - CUBE_OFFSET) / CUBE_UNIT;
            uint matId = 0;
            bool isInSolid = inSolid(probePos, matId, Cubes, FarCubes);

            if (isInSolid) {
                outPosition = currentPos;
                outNormal = float3(0, 1, 0);
                outMaterialIdx = matId;
                return true;
            }
            currentPos += rayDir * stepSize;
            tracedDistance += stepSize;
        }
        return false;
    };

    bool traceInScreenSpace(float3 position, float3 rayDir, float maxDistance, float depthTolerance, out float3 outPosition, out float3 outNormal, out uint outMaterialIdx,
                            in ConstantBuffer<UniformBufferObject> Camera, in RWTexture2D<uint2> MiniGBuffer, in StructuredBuffer<NodeProxy> NodeProxies, in StructuredBuffer<float> Vertices) 
    {
        const int maxSteps = 5;
        float stepSize = maxDistance / float(maxSteps);
        float3 rayStart = position + rayDir * 0.1;

        outPosition = float3(0, 0, 0);
        outNormal = float3(0, 0, 1);
        outMaterialIdx = 0;

        for (int i = 0; i < maxSteps; i++) {
            float3 currentPos = rayStart + rayDir * (i * stepSize);
            float4 currentPosProj = mul(Camera.ViewProjection, float4(currentPos, 1.0));
            currentPosProj.xyz /= currentPosProj.w;
            float2 currentUV = currentPosProj.xy * 0.5 + 0.5;
            if (any(currentUV < float2(0.0)) || any(currentUV > float2(1.0)))
                continue;

            int2 size;
            MiniGBuffer.GetDimensions(size.x, size.y);

            int2 sampleCoord = int2(currentUV * float2(size));
            uint2 vBufferSample = MiniGBuffer.Load(sampleCoord).rg;
            if (vBufferSample.r == 0)
                continue;
    
            float4 origin = mul(Camera.ModelViewInverse, float4(0, 0, 0, 1));
            float4 target = mul(Camera.ProjectionInverse, float4(currentPosProj.x, currentPosProj.y, 1, 1));
            float4 dir = mul(Camera.ModelViewInverse, float4(normalize(target.xyz), 0));
            float3 ray_dir = normalize(dir.xyz);

            Vertex sampleVertex = Common.get_material_data(sampleCoord, vBufferSample, origin.xyz, ray_dir, NodeProxies, Vertices);
    
            float viewSpaceCurrentDepth = mul(Camera.ModelView, float4(currentPos, 1.0)).z;
            float viewSpaceSampleDepth = mul(Camera.ModelView, float4(sampleVertex.Position, 1.0)).z;
    
            if (viewSpaceCurrentDepth < viewSpaceSampleDepth && viewSpaceCurrentDepth > viewSpaceSampleDepth - depthTolerance) {
                outPosition = sampleVertex.Position;
                outNormal = normalize(sampleVertex.Normal.rgb);
                NodeProxy hitNode = NodeProxies[vBufferSample.x - 1];
                outMaterialIdx = hitNode.matId[sampleVertex.MaterialIndex];
                return true;
            }
        }
        return false;
    };

     public bool SoftwareRayTraceScene(float3 position, float3 rayDir, float3 normal, uint sourceMaterialIdx, int2 ipos, uint4 RandomSeed, out float4 reflectionColor, in StructuredBuffer<Material> Materials,
                                       in ConstantBuffer<UniformBufferObject> Camera, in RWTexture2D<uint2> MiniGBuffer, in StructuredBuffer<NodeProxy> NodeProxies, in StructuredBuffer<float> Vertices,
                                       in RWStructuredBuffer<AmbientCube> Cubes, in RWStructuredBuffer<AmbientCube> FarCubes) {
        float3 hitPosition, hitNormal;
        uint hitMaterialIdx;
        reflectionColor = float4(0, 0, 0, 0);
        float3 offsetPos = position + normal * 0.01;
        if (traceLongDistance(offsetPos, rayDir, 5.0, 0.2, hitPosition, hitNormal, hitMaterialIdx, Camera, MiniGBuffer, NodeProxies, Vertices, Cubes, FarCubes)) {
            Material hitMaterial = Materials[hitMaterialIdx];
            float4 hitAlbedo = hitMaterial.Diffuse;
            float4 hitAmbient = interpolateProbes(hitPosition, hitNormal, Cubes, FarCubes);
            reflectionColor = hitAlbedo * hitAmbient;
            return true;
        }
        return false;
    };
}