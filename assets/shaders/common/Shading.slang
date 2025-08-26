implementing Common;
#include "PreProcessor.slang"

static const float2 haltonSeq[8] = {
    float2(0.5, 0.3333),
    float2(0.25, 0.6667),
    float2(0.75, 0.1111),
    float2(0.125, 0.4444),
    float2(0.625, 0.7778),
    float2(0.375, 0.2222),
    float2(0.875, 0.5556),
    float2(0.0625, 0.8889)
};

static const float PT_MAX_TRACE_DISTANCE = 1000.f;
static const float FAST_MAX_TRACE_DISTANCE = 20.f;
static const float GENERAL_MAX_TRACE_DISTANCE = 50.f;
static const float TRACE_CORRECTION_OFFSET = 0.001f;

namespace Common
{
    public uint GetPageIndex(float3 pagePos)
    {
        // Calculate the page index based on the page position
        uint pageIndex = uint(pagePos.x) + uint(pagePos.z) * PAGE_COUNT;
        return clamp(pageIndex, 0, 4096);
    }

    float3 CalculateRayAABBEntry(float3 rayOrigin, float3 rayDir, float3 boxMin, float3 boxMax)
    {
        float3 t1 = (boxMin - rayOrigin) / rayDir;
        float3 t2 = (boxMax - rayOrigin) / rayDir;
        
        float3 tMin = min(t1, t2);
        float3 tMax = max(t1, t2);
        
        float tNear = max(max(tMin.x, tMin.y), tMin.z);
        float tFar = min(min(tMax.x, tMax.y), tMax.z);
        
        if (all(rayOrigin >= boxMin) && all(rayOrigin <= boxMax))
            return rayOrigin;
        
        if (tNear > tFar || tFar < 0.0)
            return rayOrigin;
        
        float tEntry = max(tNear, 0.0);
        return rayOrigin + rayDir * tEntry;
    }

    public float2 CalculateMotionVectorV2(in UniformBufferObject Camera, NodeProxy node, Vertex v )
    {
        // motion info
        float4 currFrameHPos = mul(Camera.ViewProjectionUnJit, float4(v.Position, 1));
        float2 currfpos = currFrameHPos.xy / currFrameHPos.w * 0.5;
        float4 prevFrameHPos = mul(mul(Camera.PrevViewProjectionUnJit, node.combinedPrevTS), float4(v.Position, 1));
        float2 prevfpos = prevFrameHPos.xy / prevFrameHPos.w * 0.5;
        return prevfpos - currfpos;
    }

    public float2 CalculateMotionVector(ConstantBuffer<UniformBufferObject> Camera, NodeProxy node, Vertex v )
    {
        // motion info
        float4 currFrameHPos = mul(Camera.ViewProjectionUnJit, float4(v.Position, 1));
        float2 currfpos = currFrameHPos.xy / currFrameHPos.w * 0.5;
        float4 prevFrameHPos = mul(mul(Camera.PrevViewProjectionUnJit, node.combinedPrevTS), float4(v.Position, 1));
        float2 prevfpos = prevFrameHPos.xy / prevFrameHPos.w * 0.5;
        return prevfpos - currfpos;
    }

    float3 mikkTSpace(float3 vNt, float3 normal, float4 tangent)
    {
        vNt = vNt * 2.0 - 1.0;
        float3 vB = tangent.w * cross(normal, tangent.xyz);
        normal = normalize(vNt.x * tangent.xyz + vNt.y * vB + vNt.z * normal);
        return normal;
    }

    public void FetchGBufferV2(inout Vertex inVertex, in Material inMaterial, in NodeProxy proxy, float3 rayDir, out float4 outAlbedo, out float4 outNormal)
    {
        outAlbedo = inMaterial.Diffuse;

        if (inMaterial.DiffuseTextureId >= 0)
        {
            float4 tex = Bindless.GetSampleTexture(inMaterial.DiffuseTextureId).SampleLevel(inVertex.TexCoord, 0);
            outAlbedo *= tex;
        }
        float roughness = inMaterial.Fuzziness;
        if (inMaterial.MRATextureId >= 0)
        {
            float4 mra = Bindless.GetSampleTexture(inMaterial.MRATextureId).SampleLevel(inVertex.TexCoord, 0);
            roughness = roughness * mra.g;
        }

        // Normal map handling
        if (inMaterial.NormalTextureId >= 0)
        {
            // normal mapping use interpolated normal
            float3 vNt = Bindless.GetSampleTexture(inMaterial.NormalTextureId).SampleLevel(inVertex.TexCoord, 0).rgb;
            // here we need local space normal and tangent
            outNormal.rgb = normalize( mul(proxy.worldTS, float4(mikkTSpace(vNt, inVertex.LocalNormal, inVertex.LocalTangent), 0.0)).xyz );
            outNormal.a = roughness;
            inVertex.Normal.rgb = outNormal.rgb; // Update vertex normal for further calculations
        }
        else
        {
            outNormal = float4(inVertex.Normal.rgb, roughness);
        }
    }

    public void FetchGBuffer(inout Vertex inVertex, in Material inMaterial, in NodeProxy proxy, float3 rayDir, in Sampler2D[] TextureArray, out float4 outAlbedo, out float4 outNormal)
    {
        outAlbedo = inMaterial.Diffuse;

        if (inMaterial.DiffuseTextureId >= 0)
        {
            float4 tex = TextureArray[NonUniformResourceIndex(inMaterial.DiffuseTextureId)].SampleLevel(inVertex.TexCoord, 0);
            outAlbedo *= tex;
        }
        float roughness = inMaterial.Fuzziness;
        if (inMaterial.MRATextureId >= 0)
        {
            float4 mra = TextureArray[NonUniformResourceIndex(inMaterial.MRATextureId)].SampleLevel(inVertex.TexCoord, 0);
            roughness = roughness * mra.g;
        }

        // Normal map handling
        if (inMaterial.NormalTextureId >= 0)
        {
            // normal mapping use interpolated normal
            float3 vNt = TextureArray[NonUniformResourceIndex(inMaterial.NormalTextureId)].SampleLevel(inVertex.TexCoord, 0).rgb;
            // here we need local space normal and tangent
            outNormal.rgb = normalize( mul(proxy.worldTS, float4(mikkTSpace(vNt, inVertex.LocalNormal, inVertex.LocalTangent), 0.0)).xyz );
            outNormal.a = roughness;
            inVertex.Normal.rgb = outNormal.rgb; // Update vertex normal for further calculations
        }
        else
        {
            outNormal = float4(inVertex.Normal.rgb, roughness);
        }
    }

    public float3 EvaluateSH(float SHCoefficients[3][9], float3 normal, float rotate)
    {
        // Apply rotation around Y-axis (0 to 2 maps to 0 to 360 degrees)
        float angle = rotate * 3.14159265358979323846f;
        float cosAngle = cos(angle);
        float sinAngle = sin(angle);

        // Rotate the normal vector around Y-axis
        float3 rotatedNormal = float3(
            normal.x * cosAngle + normal.z * sinAngle,
            normal.y,
            -normal.x * sinAngle + normal.z * cosAngle);

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
        for (int i = 0; i < 9; ++i)
        {
            color.r += SHCoefficients[0][i] * basis[i];
            color.g += SHCoefficients[1][i] * basis[i];
            color.b += SHCoefficients[2][i] * basis[i];
        }

        return color;
    };

    public float4 SampleIBLRough(uint skyIdx, float3 direction, float rotate, in StructuredBuffer<SphericalHarmonics> HDRSHs)
    {
        float3 rayColor = EvaluateSH(HDRSHs[skyIdx].coefficients, direction, 1.0 - rotate);
        return float4(rayColor * 1.0f, 1.0);
    };

    public float4 SampleIBLV2(uint skyIdx, float3 direction, float rotate, float roughness)
    {
        float3 d = normalize(direction);
        float2 t = float2((atan2(d.x, d.z) + M_PI * rotate) * M_1_OVER_TWO_PI, acos(d.y) * M_1_PI);
        return min(float4(10, 10, 10, 1), Bindless.GetSampleTexture(skyIdx).SampleLevel(t, roughness * 8.0f));
    };

    public float4 SampleIBL(uint skyIdx, float3 direction, float rotate, float roughness, in StructuredBuffer<SphericalHarmonics> HDRSHs, in Sampler2D[] TextureSamplers)
    {
        float3 d = normalize(direction);
        float2 t = float2((atan2(d.x, d.z) + M_PI * rotate) * M_1_OVER_TWO_PI, acos(d.y) * M_1_PI);
        return min(float4(10, 10, 10, 1), TextureSamplers[NonUniformResourceIndex(skyIdx)].SampleLevel(t, roughness * 8.0f));
    };

    bool traceInScreenSpace(float3 position, float3 rayDir, float maxDistance, float depthTolerance, out float3 outPosition, out float3 outNormal, out uint outMaterialIdx,
                            in ConstantBuffer<UniformBufferObject> Camera, in RWTexture2D<uint> MiniGBuffer, in StructuredBuffer<NodeProxy> NodeProxies, in StructuredBuffer<ModelData> Models, in StructuredBuffer<GPUVertex> Vertices, in StructuredBuffer<uint> Indices)
    {
        const int maxSteps = 5;
        float stepSize = maxDistance / float(maxSteps);
        float3 rayStart = position + rayDir * 0.1;

        outPosition = float3(0, 0, 0);
        outNormal = float3(0, 0, 1);
        outMaterialIdx = 0;

        for (int i = 0; i < maxSteps; i++)
        {
            float3 currentPos = rayStart + rayDir * (i * stepSize);
            float4 currentPosProj = mul(Camera.ViewProjection, float4(currentPos, 1.0));
            currentPosProj.xyz /= currentPosProj.w;
            float2 currentUV = currentPosProj.xy * 0.5 + 0.5;
            if (any(currentUV < float2(0.0)) || any(currentUV > float2(1.0)))
                continue;

            int2 size;
            MiniGBuffer.GetDimensions(size.x, size.y);

            int2 sampleCoord = int2(currentUV * float2(size));
            uint packedValue = MiniGBuffer.Load(sampleCoord).r;
            uint2 vBufferSample = uint2((packedValue >> 17) & 0x7FFF, packedValue & 0x1FFFF);
            if (vBufferSample.r == 0)
                continue;

            float4 origin = mul(Camera.ModelViewInverse, float4(0, 0, 0, 1));
            float4 target = mul(Camera.ProjectionInverse, float4(currentPosProj.x, currentPosProj.y, 1, 1));
            float4 dir = mul(Camera.ModelViewInverse, float4(normalize(target.xyz), 0));
            float3 ray_dir = normalize(dir.xyz);

            Vertex sampleVertex = Common.get_material_data(vBufferSample, origin.xyz, ray_dir, NodeProxies, Models, Vertices, Indices);

            float viewSpaceCurrentDepth = mul(Camera.ModelView, float4(currentPos, 1.0)).z;
            float viewSpaceSampleDepth = mul(Camera.ModelView, float4(sampleVertex.Position, 1.0)).z;

            if (viewSpaceCurrentDepth < viewSpaceSampleDepth && viewSpaceCurrentDepth > viewSpaceSampleDepth - (depthTolerance))
            {
                outPosition = sampleVertex.Position;
                outNormal = normalize(sampleVertex.Normal.rgb);
                NodeProxy hitNode = NodeProxies[vBufferSample.x - 1];
                outMaterialIdx = hitNode.matId[sampleVertex.MaterialIndex];
                return true;
            }
        }
        return false;
    };
}

// emit primary ray, return hit-info
public interface IPrimaryRayCaster
{
    bool TracePrimaryRay(int2 ipos, int2 isize, inout uint4 RandomSeed, inout Vertex outVertex, inout NodeProxy outNode, inout float3 outRayDir, inout float outRayLength, inout float2 outPixelOffset);
}

// ray tracer for rendering
public interface IRayTracer
{
    bool TraceRay(float3 rayOrigin, float3 rayDir, float maxDistance, inout Vertex outVertex, inout NodeProxy outNode);
    bool TraceOcclusion(float3 rayOrigin, float3 rayDir);
    bool TraceSegment(float3 rayOrigin, float3 rayTarget, float epsilon);
}

// direct illum 
public interface IDirectIlluminator
{
    void DirectIlluminate(inout uint4 RandomSeed, float3 position, float3 normal, inout float4 directIllumColor);
}

// renderer
public interface IRenderer
{
    void Render(IRayTracer tracer, IDirectIlluminator directIllum, Vertex inVertex, float4 inAlbedo, float4 inGBuffer, in Sampler2D[] TextureArray, inout uint4 RandomSeed, inout float4 FinalColor, inout float4 FinalReflection);
}

public struct FHardwarePrimaryRayCaster : IPrimaryRayCaster
{
    public bool TracePrimaryRay(int2 ipos, int2 isize, inout uint4 RandomSeed, inout Vertex outVertex, inout NodeProxy outNode, inout float3 outRayDir, inout float outRayLength, inout float2 outPixelOffset)
    {
        float2 uvOffset = float2(0);
        float2 uv = (float2(ipos) / float2(isize)) * 2.0 - 1.0;
        if (Camera.TAA)
        {
            // here should offset a non-random offset and consider in motion vector
            uvOffset = (haltonSeq[Camera.TotalFrames % 8] - float2(0.5, 0.5)) / isize;
        }
        uv += uvOffset;
        float2 offset = Camera.Aperture / 2 * RandomInUnitDisk(RandomSeed);
        float4 origin = mul(Camera.ModelViewInverse, float4(offset, 0, 1));
        float4 target = mul(Camera.ProjectionInverse, float4(uv.x, uv.y, 1, 1));
        float4 direction = mul(Camera.ModelViewInverse, float4(normalize(target.xyz * Camera.FocusDistance - float3(offset, 0)), 0));
        outRayDir = normalize(direction.xyz);

        Material hitMaterial;
        float t = 0;
        bool hitted = HWTracer.TraceRay(origin.xyz, direction.xyz, PT_MAX_TRACE_DISTANCE, outVertex, outNode);
        if (hitted)
        {
            // 计算命中点到相机的距离
            float4 cameraPos = mul(Camera.ModelViewInverse, float4(0, 0, 0, 1));
            float hitDistance = length(outVertex.Position - cameraPos.xyz);

            // 计算与焦平面的距离差，差距为0说明在焦平面上，差距越大越模糊
            float focusDistanceDiff = abs(hitDistance - Camera.FocusDistance);

            // 根据焦距差计算散焦圆半径
            float cocRadius = 0.0f;
            if (focusDistanceDiff > 0.001f)
            {
                // CoC = (光圈大小 * 焦距差) / 命中距离
                cocRadius = (Camera.Aperture * focusDistanceDiff) / hitDistance;
            }

            // 将散焦圆半径转换为像素空间偏移
            outPixelOffset = offset * cocRadius * float2(isize) * 0.5f;
        }
        else
        {
            outPixelOffset = float2(0, 0);
        }
        return hitted;
    }

    ConstantBuffer<UniformBufferObject> Camera;
    FHardwareRayTracer HWTracer;
}

public struct FVisibilityBufferRayCasterV2 : IPrimaryRayCaster
{
    public bool TracePrimaryRay(int2 ipos, int2 isize, inout uint4 RandomSeed, inout Vertex outVertex, inout NodeProxy outNode, inout float3 outRayDir, inout float outRayLength, inout float2 outPixelOffset)
    {
        
        UniformBufferObject Camera = *Bindless.GetGpuscene().Camera;
        GPUVertex* Vertices = Bindless.GetGpuscene().Vertices;
        NodeProxy* NodeProxies = Bindless.GetGpuscene().Nodes;
        ModelData* Models = Bindless.GetGpuscene().Offsets;
        uint* PrimAddress = Bindless.GetGpuscene().Indices;

        // 第一步：先追踪一次顶点，计算距离
        uint packedValue = Bindless.GetStorageTexture<uint>(Bindless.RT_MINIGBUFFER)[ipos];
        uint2 vBuffer = uint2((packedValue >> 17) & 0x7FFF, packedValue & 0x1FFFF); 
        float2 uv = (float2(ipos) / float2(isize)) * 2.0 - 1.0;
        
        // 构造初始光线
        float4 origin = mul(Camera.ModelViewInverse, float4(0, 0, 0, 1));
        float4 target = mul(Camera.ProjectionInverse, float4(uv.x, uv.y, 1, 1));
        float4 dir = mul(Camera.ModelViewInverse, float4(normalize(target.xyz), 0));
        float3 ray_dir = normalize(dir.xyz);

        // 获取初始顶点数据
        Vertex initialVertex = Common.get_material_dataV2(vBuffer, origin.xyz, ray_dir, NodeProxies, Models, Vertices, PrimAddress);
        
        if (vBuffer.x == 0)
        {
            // 没有几何体，直接返回
            outRayDir = ray_dir;
            return false;
        }
        
        // 第二步：计算顶点到相机的距离
        float vertexDistance = length(initialVertex.Position - origin.xyz);
        
        // 第三步：根据距离和焦距计算散焦圆半径
        float cocRadius = 0.0f;
        if (abs(vertexDistance - Camera.FocusDistance) > 0.001f) // 避免除零
        {
            // 计算散焦圆半径：CoC = (光圈大小 * |物距 - 焦距|) / 物距
            cocRadius = (Camera.Aperture * abs(vertexDistance - Camera.FocusDistance)) / vertexDistance;
        }
        
        // 第四步：根据散焦圆半径计算像素偏移
        float2 pixelOffset = float2(0, 0);
        if (cocRadius > 0.001f)
        {
            // 在散焦圆内随机采样
            float2 diskSample = RandomInUnitDisk(RandomSeed);
            
            // 计算垂直于视线方向的两个点，用于确定散焦圆在屏幕空间的大小
            float3 right = normalize(cross(ray_dir, float3(0, 1, 0)));
            float3 up = normalize(cross(right, ray_dir));
            
            // 散焦圆边缘的世界坐标点
            float3 circleEdge = initialVertex.Position + right * cocRadius;
            
            // 投影到屏幕空间
            float4 centerProj = mul(Camera.ViewProjection, float4(initialVertex.Position, 1.0));
            float4 edgeProj = mul(Camera.ViewProjection, float4(circleEdge, 1.0));
            
            centerProj.xyz /= centerProj.w;
            edgeProj.xyz /= edgeProj.w;
            
            // 计算屏幕空间中的散焦圆半径（以NDC单位）
            float screenSpaceRadius = length(edgeProj.xy - centerProj.xy);
            
            // 转换为像素偏移
            pixelOffset = diskSample * screenSpaceRadius * float2(isize) * 0.5f;
        }

        outPixelOffset = pixelOffset;
        // 第五步：计算偏移后的像素位置
        int2 offsetPos = ipos + int2(pixelOffset);
        
        // 确保偏移后的位置在屏幕范围内
        offsetPos = clamp(offsetPos, int2(0, 0), isize - int2(1, 1));

        uint offsetedPackValue = Bindless.GetStorageTexture<uint>(Bindless.RT_MINIGBUFFER)[offsetPos];
        uint2 finalVBuffer = uint2((offsetedPackValue >> 17) & 0x7FFF, offsetedPackValue & 0x1FFFF); 

        float2 finalUV = (float2(offsetPos) / float2(isize)) * 2.0 - 1.0;
        
        // 构造最终光线
        float4 finalTarget = mul(Camera.ProjectionInverse, float4(finalUV.x, finalUV.y, 1, 1));
        float4 finalDir = mul(Camera.ModelViewInverse, float4(normalize(finalTarget.xyz), 0));
        float3 final_ray_dir = normalize(finalDir.xyz);
        
        // 获取最终顶点数据
        if (finalVBuffer.x == 0)
        {
            // 偏移后的位置没有几何体，使用原始结果
            outVertex.Position = initialVertex.Position;
            outVertex.Normal = normalize(initialVertex.Normal.rgb);
            outNode = NodeProxies[vBuffer.x - 1];
            outVertex.MaterialIndex = FetchMaterialId(outNode, initialVertex.MaterialIndex);
            outVertex.TexCoord = initialVertex.TexCoord;
            outVertex.Tangent = initialVertex.Tangent;
            outVertex.LocalNormal = initialVertex.LocalNormal;
            outVertex.LocalTangent = initialVertex.LocalTangent;
            outRayDir = ray_dir;
        }
        else
        {
            // 使用偏移后的结果
            Vertex finalVertex = Common.get_material_dataV2(finalVBuffer, origin.xyz, final_ray_dir, NodeProxies, Models, Vertices, PrimAddress);
            
            // 只在焦平面附近才进行深度检查
            float distanceToFocus = abs(vertexDistance - Camera.FocusDistance);
            float focusThreshold = Camera.FocusDistance * 0.1f; // 焦距的10%作为阈值
            
            bool shouldCancelOffset = false;
            if (distanceToFocus < focusThreshold)
            {
                // 检查重新采样的顶点是否比原始顶点更靠近相机
                float finalVertexDistance = length(finalVertex.Position - origin.xyz);
                float depthDifference = vertexDistance - finalVertexDistance;
                float depthThreshold = Camera.FocusDistance * 0.05f; // 深度差异阈值
                
                // 只有当深度差异超过阈值时才取消偏移
                if (depthDifference > depthThreshold)
                {
                    shouldCancelOffset = true;
                }
            }
            
            if (shouldCancelOffset)
            {
                // 取消偏移，使用原始结果
                outVertex.Position = initialVertex.Position;
                outVertex.Normal = normalize(initialVertex.Normal.rgb);
                outNode = NodeProxies[vBuffer.x - 1];
                outVertex.MaterialIndex = FetchMaterialId(outNode, initialVertex.MaterialIndex);
                outVertex.TexCoord = initialVertex.TexCoord;
                outVertex.Tangent = initialVertex.Tangent;
                outVertex.LocalNormal = initialVertex.LocalNormal;
                outVertex.LocalTangent = initialVertex.LocalTangent;
                outRayDir = ray_dir;
            }
            else
            {
                // 使用偏移后的结果
                outNode = NodeProxies[finalVBuffer.x - 1];
                
                outVertex.Position = finalVertex.Position;
                outVertex.Normal = normalize(finalVertex.Normal.rgb);
                outVertex.MaterialIndex = FetchMaterialId(outNode, finalVertex.MaterialIndex);
                outVertex.TexCoord = finalVertex.TexCoord;
                outVertex.Tangent = finalVertex.Tangent;
                outVertex.LocalNormal = finalVertex.LocalNormal;
                outVertex.LocalTangent = finalVertex.LocalTangent;
                outRayDir = final_ray_dir;
            }
        }

        outRayLength = length(outVertex.Position - origin.xyz);
        
        return true;
    }
}

public struct FVisibilityBufferRayCaster : IPrimaryRayCaster
{
    public bool TracePrimaryRay(int2 ipos, int2 isize, inout uint4 RandomSeed, inout Vertex outVertex, inout NodeProxy outNode, inout float3 outRayDir, inout float outRayLength, inout float2 outPixelOffset)
    {
        // 第一步：先追踪一次顶点，计算距离
        uint packedValue = VisibilityBuffer[ipos];
        uint2 vBuffer = uint2((packedValue >> 17) & 0x7FFF, packedValue & 0x1FFFF); 
        float2 uv = (float2(ipos) / float2(isize)) * 2.0 - 1.0;
        
        // 构造初始光线
        float4 origin = mul(Camera.ModelViewInverse, float4(0, 0, 0, 1));
        float4 target = mul(Camera.ProjectionInverse, float4(uv.x, uv.y, 1, 1));
        float4 dir = mul(Camera.ModelViewInverse, float4(normalize(target.xyz), 0));
        float3 ray_dir = normalize(dir.xyz);

        // 获取初始顶点数据
        Vertex initialVertex = Common.get_material_data(vBuffer, origin.xyz, ray_dir, NodeProxies, Models, Vertices, PrimAddress);
        
        if (vBuffer.x == 0)
        {
            // 没有几何体，直接返回
            outRayDir = ray_dir;
            return false;
        }
        
        // 第二步：计算顶点到相机的距离
        float vertexDistance = length(initialVertex.Position - origin.xyz);
        
        // 第三步：根据距离和焦距计算散焦圆半径
        float cocRadius = 0.0f;
        if (abs(vertexDistance - Camera.FocusDistance) > 0.001f) // 避免除零
        {
            // 计算散焦圆半径：CoC = (光圈大小 * |物距 - 焦距|) / 物距
            cocRadius = (Camera.Aperture * abs(vertexDistance - Camera.FocusDistance)) / vertexDistance;
        }
        
        // 第四步：根据散焦圆半径计算像素偏移
        float2 pixelOffset = float2(0, 0);
        if (cocRadius > 0.001f)
        {
            // 在散焦圆内随机采样
            float2 diskSample = RandomInUnitDisk(RandomSeed);
            
            // 计算垂直于视线方向的两个点，用于确定散焦圆在屏幕空间的大小
            float3 right = normalize(cross(ray_dir, float3(0, 1, 0)));
            float3 up = normalize(cross(right, ray_dir));
            
            // 散焦圆边缘的世界坐标点
            float3 circleEdge = initialVertex.Position + right * cocRadius;
            
            // 投影到屏幕空间
            float4 centerProj = mul(Camera.ViewProjection, float4(initialVertex.Position, 1.0));
            float4 edgeProj = mul(Camera.ViewProjection, float4(circleEdge, 1.0));
            
            centerProj.xyz /= centerProj.w;
            edgeProj.xyz /= edgeProj.w;
            
            // 计算屏幕空间中的散焦圆半径（以NDC单位）
            float screenSpaceRadius = length(edgeProj.xy - centerProj.xy);
            
            // 转换为像素偏移
            pixelOffset = diskSample * screenSpaceRadius * float2(isize) * 0.5f;
        }

        outPixelOffset = pixelOffset;
        // 第五步：计算偏移后的像素位置
        int2 offsetPos = ipos + int2(pixelOffset);
        
        // 确保偏移后的位置在屏幕范围内
        offsetPos = clamp(offsetPos, int2(0, 0), isize - int2(1, 1));

        uint offsetedPackValue = VisibilityBuffer[offsetPos];
        uint2 finalVBuffer = uint2((offsetedPackValue >> 17) & 0x7FFF, offsetedPackValue & 0x1FFFF); 

        float2 finalUV = (float2(offsetPos) / float2(isize)) * 2.0 - 1.0;
        
        // 构造最终光线
        float4 finalTarget = mul(Camera.ProjectionInverse, float4(finalUV.x, finalUV.y, 1, 1));
        float4 finalDir = mul(Camera.ModelViewInverse, float4(normalize(finalTarget.xyz), 0));
        float3 final_ray_dir = normalize(finalDir.xyz);
        
        // 获取最终顶点数据
        if (finalVBuffer.x == 0)
        {
            // 偏移后的位置没有几何体，使用原始结果
            outVertex.Position = initialVertex.Position;
            outVertex.Normal = normalize(initialVertex.Normal.rgb);
            outNode = NodeProxies[vBuffer.x - 1];
            outVertex.MaterialIndex = FetchMaterialId(outNode, initialVertex.MaterialIndex);
            outVertex.TexCoord = initialVertex.TexCoord;
            outVertex.Tangent = initialVertex.Tangent;
            outVertex.LocalNormal = initialVertex.LocalNormal;
            outVertex.LocalTangent = initialVertex.LocalTangent;
            outRayDir = ray_dir;
        }
        else
        {
            // 使用偏移后的结果
            Vertex finalVertex = Common.get_material_data(finalVBuffer, origin.xyz, final_ray_dir, NodeProxies, Models, Vertices, PrimAddress);
            
            // 只在焦平面附近才进行深度检查
            float distanceToFocus = abs(vertexDistance - Camera.FocusDistance);
            float focusThreshold = Camera.FocusDistance * 0.1f; // 焦距的10%作为阈值
            
            bool shouldCancelOffset = false;
            if (distanceToFocus < focusThreshold)
            {
                // 检查重新采样的顶点是否比原始顶点更靠近相机
                float finalVertexDistance = length(finalVertex.Position - origin.xyz);
                float depthDifference = vertexDistance - finalVertexDistance;
                float depthThreshold = Camera.FocusDistance * 0.05f; // 深度差异阈值
                
                // 只有当深度差异超过阈值时才取消偏移
                if (depthDifference > depthThreshold)
                {
                    shouldCancelOffset = true;
                }
            }
            
            if (shouldCancelOffset)
            {
                // 取消偏移，使用原始结果
                outVertex.Position = initialVertex.Position;
                outVertex.Normal = normalize(initialVertex.Normal.rgb);
                outNode = NodeProxies[vBuffer.x - 1];
                outVertex.MaterialIndex = FetchMaterialId(outNode, initialVertex.MaterialIndex);
                outVertex.TexCoord = initialVertex.TexCoord;
                outVertex.Tangent = initialVertex.Tangent;
                outVertex.LocalNormal = initialVertex.LocalNormal;
                outVertex.LocalTangent = initialVertex.LocalTangent;
                outRayDir = ray_dir;
            }
            else
            {
                // 使用偏移后的结果
                outNode = NodeProxies[finalVBuffer.x - 1];
                
                outVertex.Position = finalVertex.Position;
                outVertex.Normal = normalize(finalVertex.Normal.rgb);
                outVertex.MaterialIndex = FetchMaterialId(outNode, finalVertex.MaterialIndex);
                outVertex.TexCoord = finalVertex.TexCoord;
                outVertex.Tangent = finalVertex.Tangent;
                outVertex.LocalNormal = finalVertex.LocalNormal;
                outVertex.LocalTangent = finalVertex.LocalTangent;
                outRayDir = final_ray_dir;
            }
        }

        outRayLength = length(outVertex.Position - origin.xyz);
        
        return true;
    }

    ConstantBuffer<UniformBufferObject> Camera;
    StructuredBuffer<GPUVertex> Vertices;
    StructuredBuffer<NodeProxy> NodeProxies;
    StructuredBuffer<ModelData> Models;
    RWTexture2D<uint> VisibilityBuffer;
    StructuredBuffer<uint> PrimAddress;
}

public struct FVoxelRayCaster : IPrimaryRayCaster
{
    public bool TracePrimaryRay(int2 ipos, int2 isize, inout uint4 RandomSeed, inout Vertex outVertex, inout NodeProxy outNode, inout float3 outRayDir, inout float outRayLength, inout float2 outPixelOffset)
    {
        float2 uv = (float2(ipos) / float2(isize)) * 2.0 - 1.0;
        
        // construct ray
        float4 origin = mul(Camera.ModelViewInverse, float4(0, 0, 0, 1));
        float4 target = mul(Camera.ProjectionInverse, float4(uv.x, uv.y, 1, 1));
        float4 dir = mul(Camera.ModelViewInverse, float4(normalize(target.xyz), 0));

        float3 ray_dir = normalize(dir.xyz);
        outRayDir = ray_dir;

        return DDARayTracer.TraceRay(origin.xyz, ray_dir, 800.0f, outVertex, outNode);
    }

    ConstantBuffer<UniformBufferObject> Camera;
    FHiVoxelDDARayTracer DDARayTracer;
}

public struct FHiVoxelDDARayTracer : IRayTracer
{
    public bool TraceOcclusion(float3 rayOrigin, float3 rayDir)
    {
        Vertex outVertex;
        NodeProxy outNode;
        return TraceRay(rayOrigin, rayDir, 80.0f, outVertex, outNode);
    }
    public bool TraceSegment(float3 rayOrigin, float3 rayTaget, float epsilon)
    {
        float rayLength = length(rayTaget - rayOrigin);
        Vertex outVertex;
        NodeProxy outNode;
        return TraceRay(rayOrigin, (rayTaget - rayOrigin) / rayLength, rayLength - epsilon, outVertex, outNode);
    }
    public bool TraceRayDDA(float3 rayOrigin, float3 rayDir, float maxDistance, float3 boxMin, float3 boxMax, bool useBounds, inout Vertex outVertex, inout NodeProxy outNode)
    {
        uint hitMatId = 0;
        uint maxSteps = useBounds ? uint(16 * 4 * 1.8 * 2) : uint(maxDistance / CUBE_UNIT);
    
        float3 voxelPos = (rayOrigin.xyz - CUBE_OFFSET + float3(0.5f) * CUBE_UNIT) / CUBE_UNIT;
        float3 tDelta = abs(CUBE_UNIT / rayDir);
        float3 tMax = float3(0, 0, 0);
        float3 stepSign = sign(rayDir);
    
        if (rayDir.x != 0) tMax.x = (stepSign.x > 0 ? ceil(voxelPos.x) - voxelPos.x : voxelPos.x - floor(voxelPos.x)) * tDelta.x;
        if (rayDir.y != 0) tMax.y = (stepSign.y > 0 ? ceil(voxelPos.y) - voxelPos.y : voxelPos.y - floor(voxelPos.y)) * tDelta.y;
        if (rayDir.z != 0) tMax.z = (stepSign.z > 0 ? ceil(voxelPos.z) - voxelPos.z : voxelPos.z - floor(voxelPos.z)) * tDelta.z;
    
        bool hit = false;
        float3 normal = float3(0, 0, 0);
    
        for (uint i = 0; i < maxSteps; ++i)
        {
            if (useBounds)
            {
                float3 worldPos = CUBE_OFFSET + voxelPos * CUBE_UNIT;
                if (any(worldPos < boxMin - float3(CUBE_UNIT)) || any(worldPos > boxMax + float3(CUBE_UNIT)))
                    break;
            }
    
            float offsetDist = 0;
            float skipLength = 1;
            if (inSolid(voxelPos, rayDir, offsetDist, hitMatId, skipLength, Voxels))
            {
                hit = true;
                outVertex.Normal = normalize(normal);
                float3 voxelBoxMin = floor(voxelPos) * CUBE_UNIT + CUBE_OFFSET - float3(0.01);
                float3 voxelBoxMax = floor(voxelPos) * CUBE_UNIT + CUBE_OFFSET + float3(CUBE_UNIT);
                outVertex.Position = Common.CalculateRayAABBEntry(rayOrigin, rayDir, voxelBoxMin, voxelBoxMax) + rayDir * offsetDist;
                outVertex.MaterialIndex = hitMatId;
                outVertex.TexCoord = float2(0.5, 0.5);
                break;
            }

            if (skipLength > 1.0f)
            {
                int primaryAxis = 0;
                if (tMax.y < tMax.x && tMax.y < tMax.z) primaryAxis = 1;
                else if (tMax.z < tMax.x && tMax.z < tMax.y) primaryAxis = 2;
                
                float voxelSkipLength = skipLength * CUBE_UNIT;
                float stepsToSkip = 0; 
                stepsToSkip = floor((voxelSkipLength * abs(rayDir[primaryAxis])) / CUBE_UNIT);
                
                if (stepsToSkip >= 1.0f)
                {
                    voxelPos[primaryAxis] += stepSign[primaryAxis] * stepsToSkip;
                    tMax[primaryAxis] += tDelta[primaryAxis] * stepsToSkip;
                    
                    float timeAdvanced = tDelta[primaryAxis] * stepsToSkip;
                    
                    for (int axis = 0; axis < 3; ++axis)
                    {
                        if (axis != primaryAxis && rayDir[axis] != 0)
                        {
                            float stepsOther = timeAdvanced / tDelta[axis];
                            voxelPos[axis] += stepSign[axis] * floor(stepsOther);
                            tMax[axis] -= tDelta[axis] * floor(stepsOther);
                        }
                    }
                    
                    i += uint(stepsToSkip - 1);

                    normal = float3(0, 0, 0);
                    normal[primaryAxis] = -stepSign[primaryAxis];
                    
                    continue;
                }
            }
   
            // 原始DDA单步进
            normal = float3(0, 0, 0);
        
            if (tMax.x < tMax.y && tMax.x < tMax.z)
            {
                voxelPos.x += stepSign.x;
                tMax.x += tDelta.x;
                normal.x = -stepSign.x;
            }
            else if (tMax.y < tMax.z)
            {
                voxelPos.y += stepSign.y;
                tMax.y += tDelta.y;
                normal.y = -stepSign.y;
            }
            else
            {
                voxelPos.z += stepSign.z;
                tMax.z += tDelta.z;
                normal.z = -stepSign.z;
            }
        }
        return hit;
    }

    public bool LegacyTraceRay(float3 rayOrigin, float3 rayDir, float maxDistance, inout Vertex outVertex, inout NodeProxy outNode)
    {
        return TraceRayDDA(rayOrigin, rayDir, maxDistance, float3(0), float3(0), false, outVertex, outNode);
    }

    public bool LocalTraceRay(float3 rayOrigin, float3 rayDir, float3 boxMin, float3 boxMax, inout Vertex outVertex, inout NodeProxy outNode)
    {
        return TraceRayDDA(rayOrigin, rayDir, 0, boxMin, boxMax, true, outVertex, outNode);
    }

    public bool TraceRay(float3 rayOrigin, float3 rayDir, float maxDistance, inout Vertex outVertex, inout NodeProxy outNode)
    {
        // DDA voxel traversal
        bool hit = false;
        rayOrigin = rayOrigin + rayDir * CUBE_UNIT * 2;

        // Top Level Page Index
        float3 pagePos = (rayOrigin.xyz - PAGE_OFFSET) / PAGE_SIZE;
        float3 pagetDelta = abs(PAGE_SIZE / rayDir);
        float3 pageTMax = float3(0, 0, 0);

        uint pageIndexStartPoint = Common.GetPageIndex(pagePos);
        if (maxDistance < PAGE_SIZE * 1.7)
        {
            return LegacyTraceRay(rayOrigin, rayDir, maxDistance, outVertex, outNode);
        }

        float3 stepSign = sign(rayDir);
        if (rayDir.x != 0) pageTMax.x = (stepSign.x > 0 ? ceil(pagePos.x) - pagePos.x : pagePos.x - floor(pagePos.x)) * pagetDelta.x;
        if (rayDir.y != 0) pageTMax.y = (stepSign.y > 0 ? ceil(pagePos.y) - pagePos.y : pagePos.y - floor(pagePos.y)) * pagetDelta.y;
        if (rayDir.z != 0) pageTMax.z = (stepSign.z > 0 ? ceil(pagePos.z) - pagePos.z : pagePos.z - floor(pagePos.z)) * pagetDelta.z;
        uint maxPageSteps = uint(maxDistance / PAGE_SIZE * 1.7);

        for (uint i = 0; i < maxPageSteps; i++)
        {
            float3 boxMin = floor(pagePos) * PAGE_SIZE + PAGE_OFFSET + float3(0.01);
            float3 boxMax = floor(pagePos) * PAGE_SIZE + PAGE_OFFSET + float3(PAGE_SIZE);

            float3 localStartPos = Common.CalculateRayAABBEntry(rayOrigin, rayDir, boxMin, boxMax);

            // Check if we are in a valid page
            uint pageIndex = Common.GetPageIndex(pagePos);

            // Check if the current voxel is within the page bounds
            if (Pages[pageIndex].voxelCount > 0)
            {
                // 使用计算出的进入点进行局部追踪
                if (LocalTraceRay(localStartPos, rayDir, boxMin, boxMax, outVertex, outNode))
                {
                    hit = true;
                    break;
                }
            }

            // Step through the page
            if (pageTMax.x < pageTMax.y && pageTMax.x < pageTMax.z)
            {
                pagePos.x += stepSign.x;
                pageTMax.x += pagetDelta.x;
            }
            else if (pageTMax.y < pageTMax.z)
            {
                pagePos.y += stepSign.y;
                pageTMax.y += pagetDelta.y;
            }
            else
            {
                pagePos.z += stepSign.z;
                pageTMax.z += pagetDelta.z;
            }
        }
        return hit;
    }

    RWStructuredBuffer<VoxelData> Voxels;
    StructuredBuffer<PageIndex> Pages;
}

public struct FHardwareRayTracerV2 : IRayTracer
{
    public bool TraceOcclusion(float3 rayOrigin, float3 rayDir)
    {
        RayDesc ray;
        ray.Origin = rayOrigin;
        ray.TMin = EPS;
        ray.Direction = rayDir;
        ray.TMax = PT_MAX_TRACE_DISTANCE;
        RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES |
                 RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> q;
        let rayFlags = RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES |
                       RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH;

        q.TraceRayInline(
            Bindless.GetGpuscene().GetTLAS(),
            rayFlags,
            0xff,
            ray);

        q.Proceed();
        return q.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
    }

    public bool TraceSegment(float3 rayOrigin, float3 rayTarget, float epsilon)
    {
        float3 dir = rayTarget - rayOrigin;
        float len = length(dir);
        RayDesc ray;
        ray.Origin = rayOrigin;
        ray.TMin = epsilon;
        ray.TMax = len - epsilon;
        ray.Direction = dir / len;
        
        RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES |
                 RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> q;
        let rayFlags = RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES |
                       RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH;

        q.TraceRayInline(
            Bindless.GetGpuscene().GetTLAS(),
            rayFlags,
            0xff,
            ray);

        q.Proceed();
        return q.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
    }

    public bool TraceRay(float3 rayOrigin, float3 rayDir, float maxDistance, inout Vertex outVertex, inout NodeProxy outNode)
    {
        RayDesc ray;
        ray.Origin = rayOrigin;
        ray.TMin = EPS;
        ray.Direction = rayDir;
        ray.TMax = maxDistance;
        RayQuery<RAY_FLAG_NONE> q;
        let rayFlags = RAY_FLAG_NONE;

        q.TraceRayInline(
            Bindless.GetGpuscene().GetTLAS(),
            rayFlags,
            0xff,
            ray);

        q.Proceed();
        if (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
        {
            float t = q.CommittedRayT();

            float2 TwoBaryCoords = q.CommittedRayBarycentrics();
            float3 BaryCoords = float3(1.0 - TwoBaryCoords.x - TwoBaryCoords.y, TwoBaryCoords.x, TwoBaryCoords.y);

            float4x3 WorldToObject = q.CommittedRayWorldToObject();

            outNode = Bindless.GetGpuscene().Nodes[q.CommittedRayInstanceId()];
            outNode.instanceId = q.CommittedRayInstanceCustomIndex();
            const ModelData offsets = Bindless.GetGpuscene().Offsets[outNode.modelId];
            const uint indexOffset = offsets.indexOffset + q.CommittedPrimitiveIndex() * 3;
            const uint vertexOffset = offsets.vertexOffset;
            const Vertex v0 = UnpackVertexV2(Bindless.GetGpuscene().Vertices[vertexOffset + Bindless.GetGpuscene().Indices[indexOffset]]);
            const Vertex v1 = UnpackVertexV2(Bindless.GetGpuscene().Vertices[vertexOffset + Bindless.GetGpuscene().Indices[indexOffset + 1]]);
            const Vertex v2 = UnpackVertexV2(Bindless.GetGpuscene().Vertices[vertexOffset + Bindless.GetGpuscene().Indices[indexOffset + 2]]);
            outVertex.MaterialIndex = FetchMaterialId(outNode, v0.MaterialIndex);
            outVertex.Normal = normalize(mul(WorldToObject, Mix(v0.Normal, v1.Normal, v2.Normal, BaryCoords)).xyz);
            outVertex.TexCoord = Mix(v0.TexCoord, v1.TexCoord, v2.TexCoord, BaryCoords);
            outVertex.Position = rayOrigin + rayDir * t;// * (t - EPS2);
            //outVertex.Tangent = normalize(mul(WorldToObject, Mix(v0.Tangent, v1.Tangent, v2.Tangent, BaryCoords)).xyz);
            return true;
        }
        return false;
    }
}

public struct FHardwareRayTracer : IRayTracer
{
    public bool TraceOcclusion(float3 rayOrigin, float3 rayDir)
    {
        RayDesc ray;
        ray.Origin = rayOrigin;
        ray.TMin = EPS;
        ray.Direction = rayDir;
        ray.TMax = PT_MAX_TRACE_DISTANCE;
        RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES |
                 RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> q;
        let rayFlags = RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES |
                       RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH;

        q.TraceRayInline(
            Scene,
            rayFlags,
            0xff,
            ray);

        q.Proceed();
        return q.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
    }

    public bool TraceSegment(float3 rayOrigin, float3 rayTarget, float epsilon)
    {
        float3 dir = rayTarget - rayOrigin;
        float len = length(dir);
        RayDesc ray;
        ray.Origin = rayOrigin;
        ray.TMin = epsilon;
        ray.TMax = len - epsilon;
        ray.Direction = dir / len;
        
        RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES |
                 RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> q;
        let rayFlags = RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES |
                       RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH;

        q.TraceRayInline(
            Scene,
            rayFlags,
            0xff,
            ray);

        q.Proceed();
        return q.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
    }

    public bool TraceRay(float3 rayOrigin, float3 rayDir, float maxDistance, inout Vertex outVertex, inout NodeProxy outNode)
    {
        RayDesc ray;
        ray.Origin = rayOrigin;
        ray.TMin = EPS;
        ray.Direction = rayDir;
        ray.TMax = maxDistance;
        RayQuery<RAY_FLAG_NONE> q;
        let rayFlags = RAY_FLAG_NONE;

        q.TraceRayInline(
            Scene,
            rayFlags,
            0xff,
            ray);

        q.Proceed();
        if (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
        {
            float t = q.CommittedRayT();

            float2 TwoBaryCoords = q.CommittedRayBarycentrics();
            float3 BaryCoords = float3(1.0 - TwoBaryCoords.x - TwoBaryCoords.y, TwoBaryCoords.x, TwoBaryCoords.y);

            float4x3 WorldToObject = q.CommittedRayWorldToObject();

            outNode = NodeProxies[q.CommittedRayInstanceId()];
            outNode.instanceId = q.CommittedRayInstanceCustomIndex();
            const ModelData offsets = Offsets[outNode.modelId];
            const uint indexOffset = offsets.indexOffset + q.CommittedPrimitiveIndex() * 3;
            const uint vertexOffset = offsets.vertexOffset;
            const Vertex v0 = UnpackVertex(vertexOffset + Indices[indexOffset], Vertices);
            const Vertex v1 = UnpackVertex(vertexOffset + Indices[indexOffset + 1], Vertices);
            const Vertex v2 = UnpackVertex(vertexOffset + Indices[indexOffset + 2], Vertices);
            outVertex.MaterialIndex = FetchMaterialId(outNode, v0.MaterialIndex);
            outVertex.Normal = normalize(mul(WorldToObject, Mix(v0.Normal, v1.Normal, v2.Normal, BaryCoords)).xyz);
            outVertex.TexCoord = Mix(v0.TexCoord, v1.TexCoord, v2.TexCoord, BaryCoords);
            outVertex.Position = rayOrigin + rayDir * t;// * (t - EPS2);
            //outVertex.Tangent = normalize(mul(WorldToObject, Mix(v0.Tangent, v1.Tangent, v2.Tangent, BaryCoords)).xyz);
            return true;
        }
        return false;
    }

    StructuredBuffer<GPUVertex> Vertices;
    StructuredBuffer<uint> Indices;
    StructuredBuffer<Material> Materials;
    StructuredBuffer<ModelData> Offsets;
    StructuredBuffer<NodeProxy> NodeProxies;
    RaytracingAccelerationStructure Scene;
}

public struct FSoftwareRayTracer : IRayTracer
{
    public bool TraceOcclusion(float3 rayOrigin, float3 rayDir)
    {
        float3 offsetPos = rayOrigin;

        // Near field tracing parameters
        const float longDistanceInit = 0.5; // Far field starting distance (to avoid self-intersection)
        const float depthToleration = 0.2;   // Near field depth toleration

        // screenspace tracing is heavy, skip
        float3 outPosition;
        float3 outNormal;
        uint outMaterialIdx;
        if (Common.traceInScreenSpace(offsetPos, rayDir, longDistanceInit, depthToleration, outPosition, outNormal, outMaterialIdx, Camera, MiniGBuffer, NodeProxies, ModelDatas, Vertices, Indices))
        {
            return true;
        }

        if (DDARayTracer.TraceOcclusion(offsetPos, rayDir))
        {
            return true;
        }

        return false;
    }
    public bool TraceSegment(float3 rayOrigin, float3 rayTaget, float epsilon)
    {
        return false;
    }
    public bool TraceRay(float3 rayOrigin, float3 rayDir, float maxDistance, inout Vertex outVertex, inout NodeProxy outNode)
    {
        float3 offsetPos = rayOrigin;

        // Near field tracing parameters
        const float longDistanceInit = 0.5; // Far field starting distance (to avoid self-intersection)
        const float depthToleration = 0.2;         // Near field depth toleration

        // screenspace tracing is heavy, skip
        if (Common.traceInScreenSpace(offsetPos, rayDir, longDistanceInit, depthToleration, outVertex.Position, outVertex.Normal, outVertex.MaterialIndex, Camera, MiniGBuffer, NodeProxies, ModelDatas, Vertices, Indices))
        {
            outVertex.TexCoord = float2(0.5, 0.5);
            return true;
        }

        // use dda directly
        // offsetPos = offsetPos + rayDir * longDistanceInit; no need to offset, dda tracing will handle it
        if (DDARayTracer.TraceRay(offsetPos, rayDir, FAST_MAX_TRACE_DISTANCE, outVertex, outNode))
        {
            return true;
        }
 
        return false;
    }

    ConstantBuffer<UniformBufferObject> Camera;
    RWTexture2D<uint> MiniGBuffer;

    StructuredBuffer<GPUVertex> Vertices;
    StructuredBuffer<uint> Indices;
    StructuredBuffer<NodeProxy> NodeProxies;
    StructuredBuffer<ModelData> ModelDatas;

    FHiVoxelDDARayTracer DDARayTracer;
}

public struct FHardwareDirectIlluminatorV2 : IDirectIlluminator
{
    public void DirectIlluminate(inout uint4 RandomSeed, float3 position, float3 normal, inout float4 directIllumColor)
    {
        UniformBufferObject Camera = *Bindless.GetGpuscene().Camera;
        float shadow = 0.0f;
        const float3 lightVector = Camera.SunDirection.xyz;
        const float4 d = max(dot(lightVector, normalize(normal)), 0.0) * M_1_PI;
        if (Camera.HasSun)
        {
            const float3 lightVector = Camera.SunDirection.xyz;
            const float3 lightVectorCone = AlignWithNormal(RandomInCone(RandomSeed, cos(0.25f / 180.f * M_PI)), lightVector);
            shadow = 1;
            if (HWTracer.TraceOcclusion(position, lightVectorCone))
            {
                shadow = 0;
            }
        }
        directIllumColor = Camera.SunColor * d * shadow;
    }

    FHardwareRayTracerV2 HWTracer;
}

public struct FHardwareDirectIlluminator : IDirectIlluminator
{
    public void DirectIlluminate(inout uint4 RandomSeed, float3 position, float3 normal, inout float4 directIllumColor)
    {

        float shadow = 0.0f;
        const float3 lightVector = Camera.SunDirection.xyz;
        const float4 d = max(dot(lightVector, normalize(normal)), 0.0) * M_1_PI;
        if (Camera.HasSun)
        {
            const float3 lightVector = Camera.SunDirection.xyz;
            const float3 lightVectorCone = AlignWithNormal(RandomInCone(RandomSeed, cos(0.25f / 180.f * M_PI)), lightVector);
            shadow = 1;
            if (HWTracer.TraceOcclusion(position, lightVectorCone))
            {
                shadow = 0;
            }
        }
        directIllumColor = Camera.SunColor * d * shadow;
    }

    // all stuffs like pointers
    ConstantBuffer<UniformBufferObject> Camera;
    FHardwareRayTracer HWTracer;
}

public struct FSoftwareDirectIlluminator : IDirectIlluminator
{
    public void DirectIlluminate(inout uint4 RandomSeed, float3 position, float3 normal, inout float4 directIllumColor)
    {

        float shadow = 0.0f;
        const float3 lightVector = Camera.SunDirection.xyz;
        const float4 d = max(dot(lightVector, normalize(normal)), 0.0) * M_1_PI;
        if (Camera.HasSun)
        {
            const float3 lightVector = Camera.SunDirection.xyz;
            const float3 lightVectorCone = AlignWithNormal(RandomInCone(RandomSeed, cos(0.25f / 180.f * M_PI)), lightVector);
            shadow = 1;
            if (HWTracer.TraceOcclusion(position + normal * 0.25f, lightVectorCone))
            {
                shadow = 0;
            }
        }
        directIllumColor = Camera.SunColor * d * shadow;
    }

    // all stuffs like pointers
    ConstantBuffer<UniformBufferObject> Camera;
    FSoftwareRayTracer HWTracer;
}

public struct FShadowMapDirectIlluminator : IDirectIlluminator
{
    public float getShadow(float3 worldPos, float3 jit, float3 normal )
    {
        float4 posInLightMap = mul(Camera.SunViewProjection, float4(worldPos + jit * 4.0f, 1.0f));

        float3 projCoords = posInLightMap.xyz / posInLightMap.w;
        projCoords = projCoords * 0.5 + 0.5;
        projCoords.y = 1.0 - projCoords.y;

        float currentDepth = projCoords.z;
        float cosTheta = max(dot(normal, normalize(Camera.SunDirection.xyz)), 0.0);
        float bias = lerp(0.0001, 0.00005, cosTheta);

        float closestDepth = ShadowMapSampler.SampleLevel(projCoords.xy, 0).x;
        float shadow = currentDepth - bias > closestDepth ? 0.0 : 1.0;

        if (shadow > 0.01) {
            float3 outPosition;
            float3 outNormal;
            uint outMaterialIdx;
            float contactShadow = Common.traceInScreenSpace(worldPos + jit, normalize(Camera.SunDirection.xyz), 0.25, 0.5, outPosition, outNormal, outMaterialIdx, Camera, MiniGBuffer, NodeProxies, Models, Vertices, Indices) ? 0.0 : 1.0;
            shadow = min(shadow, contactShadow); // 结合两种阴影结果
        }

        return shadow;
    };


    public void DirectIlluminate(inout uint4 RandomSeed, float3 position, float3 normal, inout float4 directIllumColor)
    {
        float shadow = 0.0f;
        const float3 lightVector = Camera.SunDirection.xyz;
        const float4 d = max(dot(lightVector, normalize(normal)), 0.0) * M_1_PI;
        if (Camera.HasSun)
        {
            float3 jitter = (RandomFloat3(RandomSeed) - float3(0.5f)) * 0.125f;
            shadow += getShadow(position, jitter, normal);
        }
        directIllumColor = Camera.SunColor * d * shadow;
    }

    ConstantBuffer<UniformBufferObject> Camera;
    Sampler2D ShadowMapSampler;

    RWTexture2D<uint> MiniGBuffer;
    StructuredBuffer<NodeProxy> NodeProxies;
    StructuredBuffer<ModelData> Models;
    StructuredBuffer<GPUVertex> Vertices;
    StructuredBuffer<uint> Indices;
}

public struct FPathTracingRendererV2 : IRenderer
{
    bool GetRayColor(IRayTracer tracer, inout Vertex inVertex, inout float3 rayDir, inout float4 rayColor, inout uint4 RandomSeed, inout bool hitReflect, inout bool hitMetal, in Sampler2D[] TextureArray)
    {
        UniformBufferObject Camera = Bindless.GetGpuscene().Camera[0];
        Material* Materials = Bindless.GetGpuscene().Materials;

        Material mat = Materials[inVertex.MaterialIndex];
        const float startPosOffset = mat.MaterialModel == MaterialDielectric ? 0.0f : 1.0f;
        float roughness = mat.Fuzziness;
        const float dotValue = dot(rayDir, inVertex.Normal);
        const bool BackFace = dotValue > 0;
        const float3 outwardNormal = BackFace ? -inVertex.Normal : inVertex.Normal;
        const float niOverNt = BackFace ? mat.RefractionIndex2 : (1 / mat.RefractionIndex2);
        const float cosine = dotValue > 0 ? mat.RefractionIndex * dotValue : -dotValue;
        const float reflectProb = Schlick(cosine, mat.RefractionIndex);// * pow((1.0 - roughness), 4.0);
        const float metalProb = mat.Metalness;

        bool chanceReflect = (RandomFloat(RandomSeed) < reflectProb);
        bool chanceMetal = (RandomFloat(RandomSeed) < metalProb);
        bool chanceGGX = chanceReflect || chanceMetal;
        const float3 trace_next = chanceGGX ? reflect(rayDir, outwardNormal) : outwardNormal;
        float3 trace_dir = chanceGGX ? ggxSampling(RandomSeed, sqrt(roughness), trace_next) : AlignWithNormal(RandomInHemiSphere1(RandomSeed), trace_next);

        hitReflect = chanceGGX;
        hitMetal = chanceMetal;

        if (mat.MaterialModel == MaterialDielectric)
        {
            if (!chanceReflect)
            {
                trace_dir = refract(rayDir, outwardNormal, niOverNt);
            }
        }

        rayDir = trace_dir;

        NodeProxy hitNode;
        if (tracer.TraceRay(inVertex.Position + inVertex.Normal * TRACE_CORRECTION_OFFSET * startPosOffset, trace_dir, PT_MAX_TRACE_DISTANCE, inVertex, hitNode))
        {
            Material hitMat = Materials[inVertex.MaterialIndex];
            float4 outAlbedo = hitMat.Diffuse;
            if (hitMat.DiffuseTextureId >= 0)
            {
                float4 tex = TextureArray[NonUniformResourceIndex(hitMat.DiffuseTextureId)].SampleLevel(inVertex.TexCoord, 0);
                outAlbedo *= tex;
            }
            if (hitMat.MaterialModel == MaterialDiffuseLight || !chanceReflect)
            {
                rayColor *= outAlbedo;
            }
            if (BackFace && mat.MaterialModel != MaterialDielectric)
            {
                rayColor = 0.0f;
                return true;
            }
            if (hitMat.MaterialModel == MaterialDiffuseLight)
            {
                return true;
            }
            return false;
        }
        else
        {
            float4 skyColor = Camera.HasSky ? Common.SampleIBLV2(Camera.SkyIdx, trace_dir, Camera.SkyRotation, 0) * Camera.SkyIntensity : float4(0.0, 0.0, 0.0, 0.0);
            // miss, FinalColor to SkyIBL
            rayColor *= skyColor;
            return true;
        }
    }

    public void Render(IRayTracer tracer, IDirectIlluminator directIllum, Vertex inVertex, float4 inAlbedo, float4 inGBuffer, in Sampler2D[] TextureArray, inout uint4 RandomSeed, inout float4 FinalColor, inout float4 FinalReflection)
    {
        UniformBufferObject Camera = Bindless.GetGpuscene().Camera[0];
        Material* Materials = Bindless.GetGpuscene().Materials;
        AmbientCube* Cubes = Bindless.GetGpuscene().Cubes;
        VoxelData* Voxels = Bindless.GetGpuscene().Voxels;

        FinalColor = float4(0, 0, 0, 0);
        FinalReflection = float4(0, 0, 0, 0);

        Material mat = Materials[inVertex.MaterialIndex];
        if (mat.MaterialModel == MaterialDiffuseLight)
        {
            FinalColor = mat.Diffuse;
            return;
        }

        const uint sample = Camera.FastGather ? 1 : Camera.NumberOfSamples;
        for (int i = 0; i < sample; i++)
        {
            float4 RayColor = float4(1, 1, 1, 1);

            float4 eye = mul(Camera.ModelViewInverse, float4(0, 0, 0, 1));
            float3 ray_dir = normalize(inVertex.Position - eye.xyz);

            float3 direction = ray_dir;
            Vertex vertexStart = inVertex;

            uint maxBounces = mat.MaterialModel == MaterialDielectric ? Camera.MaxNumberOfBounces : Camera.NumberOfBounces;

            bool hitReflect = false;
            bool hitMetal = false;
            bool exitFirst = GetRayColor(tracer, vertexStart, direction, RayColor, RandomSeed, hitReflect, hitMetal, TextureArray);
            if (!exitFirst)
            {
                for (uint b = 1; b < maxBounces; ++b)
                {
                    bool hitReflectDontCare = false;
                    bool hitMetalDontCare = false;
                    if (GetRayColor(tracer, vertexStart, direction, RayColor, RandomSeed, hitMetalDontCare, hitMetalDontCare, TextureArray))
                    {
                        break;
                    }

                    if (!hitReflectDontCare && Camera.HasSun && (RandomFloat(RandomSeed) < 0.5f))
                    {
                        const float3 lightVector = Camera.SunDirection.xyz;
                        const float3 lightVectorCone = AlignWithNormal(RandomInCone(RandomSeed, cos(0.25f / 180.f * M_PI)), lightVector);
                        if (!tracer.TraceOcclusion(vertexStart.Position + vertexStart.Normal * TRACE_CORRECTION_OFFSET, lightVectorCone))
                        {
                            RayColor *= Camera.SunColor;
                            break;
                        }
                    }

                    // 提前退出
                    const bool earlyExit = b > 0 && (mat.MaterialModel != MaterialDielectric) && (RandomFloat(RandomSeed) < 0.5f);

                    // 终结路径
                    if (b == maxBounces - 1 || earlyExit)
                    {
                        RayColor *= interpolateAmbientCubesV2<FullAmbientCubeSampler>(vertexStart.Position, vertexStart.Normal, Cubes, Voxels);
                        break;
                    }
                }
            }

            if (hitMetal)
            {
                RayColor *= inAlbedo;
            }

            if (hitReflect)
            {
                FinalReflection += RayColor;
            }
            else
            {
                FinalColor += RayColor;
            }
            
        }

        FinalColor /= float(sample);
        FinalReflection /= float(sample);

        float4 directColor = float4(0, 0, 0, 0);
        directIllum.DirectIlluminate(RandomSeed, inVertex.Position, inVertex.Normal, directColor);
        FinalColor += directColor;
    }
}

public struct FPathTracingRenderer : IRenderer
{
    bool GetRayColor(IRayTracer tracer, inout Vertex inVertex, inout float3 rayDir, inout float4 rayColor, inout uint4 RandomSeed, inout bool hitReflect, inout bool hitMetal, in Sampler2D[] TextureArray)
    {
        Material mat = Materials[inVertex.MaterialIndex];
        const float startPosOffset = mat.MaterialModel == MaterialDielectric ? 0.0f : 1.0f;
        float roughness = mat.Fuzziness;
        const float dotValue = dot(rayDir, inVertex.Normal);
        const bool BackFace = dotValue > 0;
        const float3 outwardNormal = BackFace ? -inVertex.Normal : inVertex.Normal;
        const float niOverNt = BackFace ? mat.RefractionIndex2 : (1 / mat.RefractionIndex2);
        const float cosine = dotValue > 0 ? mat.RefractionIndex * dotValue : -dotValue;
        const float reflectProb = Schlick(cosine, mat.RefractionIndex);// * pow((1.0 - roughness), 4.0);
        const float metalProb = mat.Metalness;

        bool chanceReflect = (RandomFloat(RandomSeed) < reflectProb);
        bool chanceMetal = (RandomFloat(RandomSeed) < metalProb);
        bool chanceGGX = chanceReflect || chanceMetal;
        const float3 trace_next = chanceGGX ? reflect(rayDir, outwardNormal) : outwardNormal;
        float3 trace_dir = chanceGGX ? ggxSampling(RandomSeed, sqrt(roughness), trace_next) : AlignWithNormal(RandomInHemiSphere1(RandomSeed), trace_next);

        hitReflect = chanceGGX;
        hitMetal = chanceMetal;

        if (mat.MaterialModel == MaterialDielectric)
        {
            if (!chanceReflect)
            {
                trace_dir = refract(rayDir, outwardNormal, niOverNt);
            }
        }

        rayDir = trace_dir;

        NodeProxy hitNode;
        if (tracer.TraceRay(inVertex.Position + inVertex.Normal * TRACE_CORRECTION_OFFSET * startPosOffset, trace_dir, PT_MAX_TRACE_DISTANCE, inVertex, hitNode))
        {
            Material hitMat = Materials[inVertex.MaterialIndex];
            float4 outAlbedo = hitMat.Diffuse;
            if (hitMat.DiffuseTextureId >= 0)
            {
                float4 tex = TextureArray[NonUniformResourceIndex(hitMat.DiffuseTextureId)].SampleLevel(inVertex.TexCoord, 0);
                outAlbedo *= tex;
            }
            if (hitMat.MaterialModel == MaterialDiffuseLight || !chanceReflect)
            {
                rayColor *= outAlbedo;
            }
            if (BackFace && mat.MaterialModel != MaterialDielectric)
            {
                rayColor = 0.0f;
                return true;
            }
            if (hitMat.MaterialModel == MaterialDiffuseLight)
            {
                return true;
            }
            return false;
        }
        else
        {
            float4 skyColor = Camera.HasSky ? Common.SampleIBL(Camera.SkyIdx, trace_dir, Camera.SkyRotation, 0, HDRSHs, TextureArray) * Camera.SkyIntensity : float4(0.0, 0.0, 0.0, 0.0);
            // miss, FinalColor to SkyIBL
            rayColor *= skyColor;
            return true;
        }
    }

    public void Render(IRayTracer tracer, IDirectIlluminator directIllum, Vertex inVertex, float4 inAlbedo, float4 inGBuffer, in Sampler2D[] TextureArray, inout uint4 RandomSeed, inout float4 FinalColor, inout float4 FinalReflection)
    {
        FinalColor = float4(0, 0, 0, 0);
        FinalReflection = float4(0, 0, 0, 0);

        Material mat = Materials[inVertex.MaterialIndex];
        if (mat.MaterialModel == MaterialDiffuseLight)
        {
            FinalColor = mat.Diffuse;
            return;
        }

        const uint sample = Camera.FastGather ? 1 : Camera.NumberOfSamples;
        for (int i = 0; i < sample; i++)
        {
            float4 RayColor = float4(1, 1, 1, 1);

            float4 eye = mul(Camera.ModelViewInverse, float4(0, 0, 0, 1));
            float3 ray_dir = normalize(inVertex.Position - eye.xyz);

            float3 direction = ray_dir;
            Vertex vertexStart = inVertex;

            uint maxBounces = mat.MaterialModel == MaterialDielectric ? Camera.MaxNumberOfBounces : Camera.NumberOfBounces;

            bool hitReflect = false;
            bool hitMetal = false;
            bool exitFirst = GetRayColor(tracer, vertexStart, direction, RayColor, RandomSeed, hitReflect, hitMetal, TextureArray);
            if (!exitFirst)
            {
                for (uint b = 1; b < maxBounces; ++b)
                {
                    bool hitReflectDontCare = false;
                    bool hitMetalDontCare = false;
                    if (GetRayColor(tracer, vertexStart, direction, RayColor, RandomSeed, hitMetalDontCare, hitMetalDontCare, TextureArray))
                    {
                        break;
                    }

                    if (!hitReflectDontCare && Camera.HasSun && (RandomFloat(RandomSeed) < 0.5f))
                    {
                        const float3 lightVector = Camera.SunDirection.xyz;
                        const float3 lightVectorCone = AlignWithNormal(RandomInCone(RandomSeed, cos(0.25f / 180.f * M_PI)), lightVector);
                        if (!tracer.TraceOcclusion(vertexStart.Position + vertexStart.Normal * TRACE_CORRECTION_OFFSET, lightVectorCone))
                        {
                            RayColor *= Camera.SunColor;
                            break;
                        }
                    }

                    // 提前退出
                    const bool earlyExit = b > 0 && (mat.MaterialModel != MaterialDielectric) && (RandomFloat(RandomSeed) < 0.5f);

                    // 终结路径
                    if (b == maxBounces - 1 || earlyExit)
                    {
                        RayColor *= interpolateAmbientCubes<FullAmbientCubeSampler>(vertexStart.Position, vertexStart.Normal, Cubes, Voxels);
                        break;
                    }
                }
            }

            if (hitMetal)
            {
                RayColor *= inAlbedo;
            }

            if (hitReflect)
            {
                FinalReflection += RayColor;
            }
            else
            {
                FinalColor += RayColor;
            }
            
        }

        FinalColor /= float(sample);
        FinalReflection /= float(sample);

        float4 directColor = float4(0, 0, 0, 0);
        directIllum.DirectIlluminate(RandomSeed, inVertex.Position, inVertex.Normal, directColor);
        FinalColor += directColor;
    }

    ConstantBuffer<UniformBufferObject> Camera;
    StructuredBuffer<GPUVertex> Vertices;
    StructuredBuffer<uint> Indices;
    StructuredBuffer<Material> Materials;
    StructuredBuffer<ModelData> Offsets;
    StructuredBuffer<NodeProxy> NodeProxies;

    RWStructuredBuffer<AmbientCube> Cubes;
    RWStructuredBuffer<VoxelData> Voxels;
    StructuredBuffer<SphericalHarmonics> HDRSHs;
}

public struct FHybridRenderer : IRenderer
{
    public void Render(IRayTracer tracer, IDirectIlluminator directIllum, Vertex inVertex, float4 inAlbedo, float4 inGBuffer, in Sampler2D[] TextureArray, inout uint4 RandomSeed, inout float4 FinalLambertation, inout float4 FinalReflection)
    {
        // construct ray
        float4 eye = mul(Camera.ModelViewInverse, float4(0, 0, 0, 1));
        float3 ray_dir = normalize(inVertex.Position - eye.xyz);

        Material mat = Materials[inVertex.MaterialIndex];
        if (mat.MaterialModel == MaterialDiffuseLight)
        {
            FinalLambertation = mat.Diffuse;
            FinalReflection = float4(0, 0, 0, 0);
            return;
        }
        float roughness = inGBuffer.a;

        // ibl
        const float dotValue = dot(ray_dir, inVertex.Normal);
        const float3 outwardNormal = dotValue > 0 ? -inVertex.Normal : inVertex.Normal;
        const float cosine = dotValue > 0 ? mat.RefractionIndex * dotValue : -dotValue;
        const float reflectProb = Schlick(cosine, mat.RefractionIndex);// * pow((1.0 - roughness), 4.0);
        const float metalProb = mat.Metalness;

        FinalLambertation = float4(0, 0, 0, 0);
        FinalReflection = float4(0, 0, 0, 0);

        //const uint jitcount = Camera.FastGather ? 1 : max(1, Camera.NumberOfSamples / 4);
        const uint jitcount = Camera.FastGather ? 1 : max(1, Camera.NumberOfSamples);
        for (int i = 0; i < jitcount; i++)
        {
            float4 rayColor = float4(0, 0, 0, 1);
            bool chanceReflect = (RandomFloat(RandomSeed) < reflectProb);
            bool chanceMetal = (RandomFloat(RandomSeed) < metalProb);
            bool chanceGGX = chanceReflect || chanceMetal;
            const float3 trace_next = chanceGGX ? reflect(ray_dir, outwardNormal) : outwardNormal;
            float3 trace_dir = chanceGGX ? ggxSampling(RandomSeed, sqrt(roughness), trace_next) : AlignWithNormal(RandomInHemiSphere1(RandomSeed), trace_next);

            float4 reflectionColor;
            Vertex hitVertex;
            NodeProxy hitNode;
            if (tracer.TraceRay(inVertex.Position + inVertex.Normal * TRACE_CORRECTION_OFFSET, trace_dir, GENERAL_MAX_TRACE_DISTANCE, hitVertex, hitNode))
            {
                Material hitMat = Materials[hitVertex.MaterialIndex];
                float4 outAlbedo = hitMat.Diffuse;
                if (hitMat.MaterialModel == MaterialDiffuseLight)
                {
                    rayColor = outAlbedo;
                }
                else
                {
                    if (chanceReflect && !chanceMetal)
                    {
                        outAlbedo = float4(1, 1, 1, 1);
                    }
                    rayColor = outAlbedo * interpolateAmbientCubes<FullAmbientCubeSampler>(hitVertex.Position, hitVertex.Normal, Cubes, Voxels);
                }
            }
            else
            {
                float4 skyAffect = float4(0, 0, 0, 0);
                // 这里非常影响噪点，直接使用IBL数据，是最精确的。如果使用预处理数据，由于IBL的明度变化不大，因此噪点会小，但丢失光照细节
                if (Camera.FastGather)
                {
                    float simpleRoughness = chanceGGX ? roughness : 1.0;
                    rayColor = Camera.HasSky ? Common.SampleIBL(Camera.SkyIdx, trace_next, Camera.SkyRotation, simpleRoughness, HDRSHs, TextureArray) * Camera.SkyIntensity : float4(0.0, 0.0, 0.0, 0.0);
                }
                else
                {
                    rayColor = Camera.HasSky ? Common.SampleIBL(Camera.SkyIdx, trace_dir, Camera.SkyRotation, 0, HDRSHs, TextureArray) * Camera.SkyIntensity : float4(0.0, 0.0, 0.0, 0.0);
                }
            }

            if (chanceMetal)
            {
                rayColor *= inAlbedo;
            }

            if (chanceGGX)
            {
                FinalReflection += rayColor;
            }
            else
            {
                FinalLambertation += rayColor;
            }
        }
        FinalReflection /= float(jitcount);
        FinalLambertation /= float(jitcount);

        float4 directColor = float4(0, 0, 0, 0);
        directIllum.DirectIlluminate(RandomSeed, inVertex.Position, inVertex.Normal, directColor);
        FinalLambertation += directColor;
    }

    ConstantBuffer<UniformBufferObject> Camera;
    StructuredBuffer<GPUVertex> Vertices;
    StructuredBuffer<uint> Indices;
    StructuredBuffer<Material> Materials;
    StructuredBuffer<ModelData> Offsets;
    StructuredBuffer<NodeProxy> NodeProxies;

    RWStructuredBuffer<AmbientCube> Cubes;
    RWStructuredBuffer<VoxelData> Voxels;
    StructuredBuffer<SphericalHarmonics> HDRSHs;
}
