#include "PreProcessor.slang"

static const float PT_MAX_TRACE_DISTANCE = 1000.f;
static const float FAST_MAX_TRACE_DISTANCE = 20.f;
static const float GENERAL_MAX_TRACE_DISTANCE = 50.f;
static const float TRACE_CORRECTION_OFFSET = 0.0f;
static const uint OBJECT_ID_FLAG_VALID = 0x80000000u;
static const uint OBJECT_ID_FLAG_SELECTED = 0x40000000u;
static const uint OBJECT_ID_FLAG_HOVERED = 0x20000000u;
static const uint OBJECT_ID_FLAG_LOCKED = 0x10000000u;
static const uint OBJECT_ID_FLAG_DANGER = 0x08000000u;
static const uint OBJECT_ID_INSTANCE_MASK = 0x07FFFFFFu;

namespace Common
{
    public uint EncodeObjectId(uint instanceId, bool selected, bool hovered, bool locked, bool danger)
    {
        uint objectId = OBJECT_ID_FLAG_VALID | (instanceId & OBJECT_ID_INSTANCE_MASK);
        if (selected)
        {
            objectId |= OBJECT_ID_FLAG_SELECTED;
        }
        if (hovered)
        {
            objectId |= OBJECT_ID_FLAG_HOVERED;
        }
        if (locked)
        {
            objectId |= OBJECT_ID_FLAG_LOCKED;
        }
        if (danger)
        {
            objectId |= OBJECT_ID_FLAG_DANGER;
        }
        return objectId;
    }

    public uint GetPageIndex(float3 pagePos)
    {
        // Calculate the page index based on the page position
        uint pageIndex = uint(pagePos.x) + uint(pagePos.z) * PAGE_COUNT;
        return clamp(pageIndex, 0, 4095);
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

    public float2 CalculateMotionVector(in UniformBufferObject Camera, NodeProxy node, Vertex v, int2 ipos )
    {
        // motion info
        float4 currFrameHPos = mul(Camera.ViewProjectionUnJit, float4(v.Position, 1));
        float2 currfpos = currFrameHPos.xy / currFrameHPos.w * 0.5;
        float4 prevFrameHPos = mul(mul(Camera.PrevViewProjectionUnJit, node.combinedPrevTS), float4(v.Position, 1));
        float2 prevfpos = prevFrameHPos.xy / prevFrameHPos.w * 0.5;

        let MotionMoment = Bindless.GetViewStorageTexture<uint>(Bindless.RT_MOTIONMOMENT);
        

        float4 prevPos = mul(node.combinedPrevTS, float4(v.Position, 1));
        if( distance(v.Position.xyz, prevPos.xyz) > 0.02)
        {
            MotionMoment[ipos] = Bindless.GetGpuscene().Camera[0].TemporalFrames;
        }
        else
        {
            MotionMoment[ipos] = MotionMoment[ipos] > 0 ? MotionMoment[ipos] - 1 : 0;
        }

        float2 motion = prevfpos - currfpos;
        // mark invalid if motion objects
        // return MotionMoment[ipos] > 0 ? 3.40282346638528859811704183484516925440e38f : motion;
        return motion;
    }

    float3 mikkTSpace(float3 vNt, float3 normal, float4 tangent)
    {
        vNt = vNt * 2.0 - 1.0;
        float3 vB = tangent.w * cross(normal, tangent.xyz);
        normal = normalize(vNt.x * tangent.xyz + vNt.y * vB + vNt.z * normal);
        return normal;
    }

    public void FetchGBuffer(inout Vertex inVertex, in Material inMaterial, in NodeProxy proxy, float3 rayDir, out float4 outAlbedo, out float4 outNormal)
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

    float3 EvaluateSHWithBandWeights(
        float SHCoefficients[3][9],
        float3 direction,
        float rotate,
        float3 bandWeights)
    {
        // Apply rotation around Y-axis (0 to 2 maps to 0 to 360 degrees)
        float angle = rotate * 3.14159265358979323846f;
        float cosAngle = cos(angle);
        float sinAngle = sin(angle);

        float3 rotatedDirection = float3(
            direction.x * cosAngle + direction.z * sinAngle,
            direction.y,
            -direction.x * sinAngle + direction.z * cosAngle);

        // SH basis function evaluation
        static const float SH_C0 = 0.282095f;
        static const float SH_C1 = 0.488603f;
        static const float SH_C2 = 1.092548f;
        static const float SH_C3 = 0.315392f;
        static const float SH_C4 = 0.546274f;

        float basis[9];
        basis[0] = SH_C0;
        basis[1] = -SH_C1 * rotatedDirection.y;
        basis[2] = SH_C1 * rotatedDirection.z;
        basis[3] = -SH_C1 * rotatedDirection.x;
        basis[4] = SH_C2 * rotatedDirection.x * rotatedDirection.y;
        basis[5] = -SH_C2 * rotatedDirection.y * rotatedDirection.z;
        basis[6] = SH_C3 * (3.f * rotatedDirection.y * rotatedDirection.y - 1.0f);
        basis[7] = -SH_C2 * rotatedDirection.x * rotatedDirection.z;
        basis[8] = SH_C4 * (rotatedDirection.x * rotatedDirection.x - rotatedDirection.z * rotatedDirection.z);

        float3 color = float3(0.0, 0.0, 0.0);
        for (int i = 0; i < 9; ++i)
        {
            float bandWeight = i == 0 ? bandWeights.x : (i <= 3 ? bandWeights.y : bandWeights.z);
            color.r += SHCoefficients[0][i] * basis[i] * bandWeight;
            color.g += SHCoefficients[1][i] * basis[i] * bandWeight;
            color.b += SHCoefficients[2][i] * basis[i] * bandWeight;
        }

        return color;
    };

    public float3 EvaluateSH(float SHCoefficients[3][9], float3 direction, float rotate)
    {
        return EvaluateSHWithBandWeights(SHCoefficients, direction, rotate, float3(1.0f));
    }

    public float3 EvaluateDiffuseSH(float SHCoefficients[3][9], float3 normal, float rotate)
    {
        // Lambert cosine convolution divided by pi, yielding reflected radiance
        // for a unit-albedo diffuse surface.
        return EvaluateSHWithBandWeights(SHCoefficients, normal, rotate, float3(1.0f, 2.0f / 3.0f, 0.25f));
    }

    public float4 SampleIBLRough(uint skyIdx, float3 direction, float rotate)
    {
        float3 rayColor = EvaluateSH(Bindless.GetGpuscene().HDRSHs[skyIdx].coefficients, direction, rotate);
        return float4(rayColor * 1.0f, 1.0);
    };

    public float4 SampleIBLDiffuse(uint skyIdx, float3 normal, float rotate)
    {
        float3 irradiance = EvaluateDiffuseSH(
            Bindless.GetGpuscene().HDRSHs[skyIdx].coefficients, normalize(normal), rotate);
        return float4(max(irradiance, float3(0.0f)), 1.0f);
    }

    public float4 SampleIBL(uint skyIdx, float3 direction, float rotate, float roughness)
    {
        float3 d = normalize(direction);
        float2 t = float2((atan2(d.x, d.z) + M_PI * rotate) * M_1_OVER_TWO_PI, acos(d.y) * M_1_PI);
        return min(float4(10, 10, 10, 1), Bindless.GetSampleTexture(skyIdx).SampleLevel(t, roughness * 8.0f));
    };

    bool traceInScreenSpace(float3 position, float3 rayDir, float maxDistance, float depthTolerance, out float3 outPosition, out float3 outNormal, out uint outMaterialIdx)
    {
        UniformBufferObject Camera = Bindless.GetGpuscene().Camera[0];
        GPUVertex* Vertices = Bindless.GetGpuscene().Vertices;
        NodeProxy* NodeProxies = Bindless.GetGpuscene().Nodes;
        ModelData* Models = Bindless.GetGpuscene().Offsets;
        uint* PrimAddress = Bindless.GetGpuscene().Indices;

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

            int2 size = Bindless.GetViewStorageTextureDimensions<uint>(Bindless.RT_MINIGBUFFER);

            int2 sampleCoord = int2(currentUV * float2(size));
            uint packedValue = Bindless.GetViewStorageTexture<uint>(Bindless.RT_MINIGBUFFER).Load(sampleCoord).r;
            uint2 vBufferSample = uint2((packedValue >> 17) & 0x7FFF, packedValue & 0x1FFFF);
            if (vBufferSample.r == 0)
                continue;

            float4 origin = mul(Camera.ModelViewInverse, float4(0, 0, 0, 1));
            float4 target = mul(Camera.ProjectionInverse, float4(currentPosProj.x, currentPosProj.y, 1, 1));
            float4 dir = mul(Camera.ModelViewInverse, float4(normalize(target.xyz), 0));
            float3 ray_dir = normalize(dir.xyz);

            Vertex sampleVertex = Common.get_material_data(vBufferSample, origin.xyz, ray_dir, NodeProxies, Models, Vertices, PrimAddress);

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
    bool TraceRay(float3 rayOrigin, float3 rayDir, float maxDistance, inout Vertex outVertex, inout NodeProxy outNode, inout float4 inoutRayColor, out float outHitDist);
    bool TraceOcclusion(float3 rayOrigin, float3 rayDir);
    bool TraceSegment(float3 rayOrigin, float3 rayTarget, float epsilon);
}

// direct illum
public interface IDirectIlluminator
{
    void DirectIlluminate(inout uint4 RandomSeed, float3 position, float3 normal, inout float4 directIllumColor);

    // Primary-surface variant. Identical to DirectIlluminate unless a screen-space resampler
    // (ReSTIR) replaces part of the estimate; pixel addresses the per-pixel reservoir.
    void DirectIlluminatePrimary(inout uint4 RandomSeed, int2 pixel, float3 position, float3 normal, inout float4 directIllumColor);
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
            uvOffset = Camera.Jitter.xy * 2.0 / float2(isize);
        }
        uv += uvOffset;
        float2 offset = Camera.Aperture / 2 * RandomInUnitDisk(RandomSeed);
        float4 origin = mul(Camera.ModelViewInverse, float4(offset, 0, 1));
        float4 target = mul(Camera.ProjectionInverse, float4(uv.x, uv.y, 1, 1));
        float4 direction = mul(Camera.ModelViewInverse, float4(normalize(target.xyz * Camera.FocusDistance - float3(offset, 0)), 0));
        outRayDir = normalize(direction.xyz);

        Material hitMaterial;
        float t = 0;
        float4 tempColor;
        float hitDist = 0;
        bool hit = HWTracer.TraceRay(origin.xyz, direction.xyz, PT_MAX_TRACE_DISTANCE, outVertex, outNode, tempColor, hitDist);
        if (hit)
        {
            // 计算命中点到相机的距离
            float4 cameraPos = mul(Camera.ModelViewInverse, float4(0, 0, 0, 1));
            float hitDistance = hitDist;

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
        return hit;
    }

    ConstantBuffer<UniformBufferObject> Camera;
    FHardwareRayTracer HWTracer;
}

public struct FVisibilityBufferRayCaster : IPrimaryRayCaster
{
    public bool TracePrimaryRay(int2 ipos, int2 isize, inout uint4 RandomSeed, inout Vertex outVertex, inout NodeProxy outNode, inout float3 outRayDir, inout float outRayLength, inout float2 outPixelOffset)
    {
        let Camera = Bindless.GetGpuscene().Camera[0];
        GPUVertex* vertices = Bindless.GetGpuscene().Vertices;
        NodeProxy* nodes = Bindless.GetGpuscene().Nodes;
        ModelData* models = Bindless.GetGpuscene().Offsets;
        uint* indices = Bindless.GetGpuscene().Indices;

        const uint packedValue = Bindless.GetViewStorageTexture<uint>(Bindless.RT_MINIGBUFFER)[ipos];
        const uint2 vBuffer = uint2((packedValue >> 17) & 0x7FFF, packedValue & 0x1FFFF);
        const float2 uv = (float2(ipos) / float2(isize)) * 2.0f - 1.0f;
        const float4 origin = mul(Camera.ModelViewInverse, float4(0, 0, 0, 1));
        const float4 target = mul(Camera.ProjectionInverse, float4(uv.x, uv.y, 1, 1));
        const float4 dir = mul(Camera.ModelViewInverse, float4(normalize(target.xyz), 0));
        const float3 rayDir = normalize(dir.xyz);

        outRayDir = rayDir;
        outPixelOffset = float2(0, 0);
        if (vBuffer.x == 0)
        {
            return false;
        }

        outVertex = Common.get_material_data(vBuffer, origin.xyz, rayDir, nodes, models, vertices, indices);
        outNode = nodes[vBuffer.x - 1];
        outVertex.MaterialIndex = FetchMaterialId(outNode, outVertex.MaterialIndex);
        outRayLength = length(outVertex.Position - origin.xyz);
        return true;
    }

#if 0
    public bool TracePrimaryRayDofLegacy(int2 ipos, int2 isize, inout uint4 RandomSeed, inout Vertex outVertex, inout NodeProxy outNode, inout float3 outRayDir, inout float outRayLength, inout float2 outPixelOffset)
    {
        
        UniformBufferObject Camera = *Bindless.GetGpuscene().Camera;
        GPUVertex* Vertices = Bindless.GetGpuscene().Vertices;
        NodeProxy* NodeProxies = Bindless.GetGpuscene().Nodes;
        ModelData* Models = Bindless.GetGpuscene().Offsets;
        uint* PrimAddress = Bindless.GetGpuscene().Indices;

        // 第一步：先追踪一次顶点，计算距离
        uint packedValue = Bindless.GetViewStorageTexture<uint>(Bindless.RT_MINIGBUFFER)[ipos];
        uint2 vBuffer = uint2((packedValue >> 17) & 0x7FFF, packedValue & 0x1FFFF); 
        float2 uv = (float2(ipos) / float2(isize)) * 2.0 - 1.0;
        
        // 构造初始光线
        float4 origin = mul(Camera.ModelViewInverse, float4(0, 0, 0, 1));
        float4 target = mul(Camera.ProjectionInverse, float4(uv.x, uv.y, 1, 1));
        float4 dir = mul(Camera.ModelViewInverse, float4(normalize(target.xyz), 0));
        float3 ray_dir = normalize(dir.xyz);

        if (vBuffer.x == 0)
        {
            // 没有几何体，直接返回
            outRayDir = ray_dir;
            return false;
        }

        // 获取初始顶点数据
        Vertex initialVertex = Common.get_material_data(vBuffer, origin.xyz, ray_dir, NodeProxies, Models, Vertices, PrimAddress);
        
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

        uint offsetPackValue = Bindless.GetViewStorageTexture<uint>(Bindless.RT_MINIGBUFFER)[offsetPos];
        uint2 finalVBuffer = uint2((offsetPackValue >> 17) & 0x7FFF, offsetPackValue & 0x1FFFF); 

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
#endif
}
public struct FVoxelRayCaster : IPrimaryRayCaster
{
    public bool TracePrimaryRay(int2 ipos, int2 isize, inout uint4 RandomSeed, inout Vertex outVertex, inout NodeProxy outNode, inout float3 outRayDir, inout float outRayLength, inout float2 outPixelOffset)
    {
        let Camera = Bindless.GetGpuscene().Camera[0];

        float2 uv = (float2(ipos) / float2(isize)) * 2.0 - 1.0;
        
        // construct ray
        float4 origin = mul(Camera.ModelViewInverse, float4(0, 0, 0, 1));
        float4 target = mul(Camera.ProjectionInverse, float4(uv.x, uv.y, 1, 1));
        float4 dir = mul(Camera.ModelViewInverse, float4(normalize(target.xyz), 0));

        float3 ray_dir = normalize(dir.xyz);
        outRayDir = ray_dir;

        float4 tempColor;
        float hitDist = 0;
        return DDARayTracer.TraceRay(origin.xyz, ray_dir, 800.0f, outVertex, outNode, tempColor, hitDist);
    }

    FHiVoxelDDARayTracer DDARayTracer;
}
