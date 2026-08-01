implementing PathTracing;

#include "PreProcessor.slang"
import Shader.VisibilityBuffer;

// IPrimaryRayCaster implementations: hardware ray-query, visibility-buffer replay and
// voxel DDA. Interfaces live in Interfaces.slang; ray tracers in RayTracers.slang.

public struct FHardwarePrimaryRayCaster : IPrimaryRayCaster
{
    public bool TracePrimaryRay(int2 ipos, int2 isize, inout uint4 RandomSeed, inout Vertex outVertex, inout NodeProxy outNode, inout float3 outRayDir, inout float outRayLength, inout float2 outPixelOffset)
    {
        let Camera = Bindless.GetGpuscene().Camera[0];
        float2 uvOffset = float2(0);
        float2 uv = ((float2(ipos) + 0.5f) / float2(isize)) * 2.0f - 1.0f;
        float2 offset = Camera.Aperture / 2 * RandomInUnitDisk(RandomSeed);
        float4 origin = mul(Camera.ModelViewInverse, float4(offset, 0, 1));
        float4 target = mul(Camera.ProjectionInverse, float4(uv.x, uv.y, 1, 1));
        float4 direction = mul(Camera.ModelViewInverse, float4(normalize(target.xyz * Camera.FocusDistance - float3(offset, 0)), 0));
        outRayDir = normalize(direction.xyz);

        FHardwareRayTracer tracer;
        float4 tempColor;
        float hitDist = 0;
        bool hit = tracer.TraceRay(origin.xyz, outRayDir, PT_MAX_TRACE_DISTANCE, outVertex, outNode, tempColor, hitDist);
        if (hit)
        {
            outRayLength = hitDist;

            // 计算与焦平面的距离差，差距为0说明在焦平面上，差距越大越模糊
            float focusDistanceDiff = abs(hitDist - Camera.FocusDistance);

            // 根据焦距差计算散焦圆半径
            float cocRadius = 0.0f;
            if (focusDistanceDiff > 0.001f)
            {
                // CoC = (光圈大小 * 焦距差) / 命中距离
                cocRadius = (Camera.Aperture * focusDistanceDiff) / hitDist;
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

        const uint2 vBuffer = Visibility.Load(ipos, gpuScene.CustomData0);
        // Vulkan raster coverage is evaluated at pixel centers. The reconstruction
        // ray must use that exact sample or it can leave the stored triangle at
        // grazing angles.
        const float2 uv = ((float2(ipos) + 0.5f) / float2(isize)) * 2.0f - 1.0f;
        const float4 origin = mul(Camera.ModelViewInverse, float4(0, 0, 0, 1));
        const float4 target = mul(Camera.ProjectionInverse, float4(uv.x, uv.y, 1, 1));
        const float4 dir = mul(Camera.ModelViewInverse, float4(normalize(target.xyz), 0));
        const float3 rayDir = normalize(dir.xyz);

        outRayDir = rayDir;
        outPixelOffset = float2(0, 0);
        if (!Visibility.IsValid(vBuffer))
        {
            return false;
        }

        if (!Common.get_material_data(
                vBuffer, origin.xyz, rayDir, nodes, models, vertices, indices, outVertex))
        {
            return false;
        }
        outNode = nodes[vBuffer.x - 1];
        outVertex.MaterialIndex = FetchMaterialId(outNode, outVertex.MaterialIndex);
        outRayLength = length(outVertex.Position - origin.xyz);
        return true;
    }

}
public struct FVoxelRayCaster : IPrimaryRayCaster
{
    public bool TracePrimaryRay(int2 ipos, int2 isize, inout uint4 RandomSeed, inout Vertex outVertex, inout NodeProxy outNode, inout float3 outRayDir, inout float outRayLength, inout float2 outPixelOffset)
    {
        let Camera = Bindless.GetGpuscene().Camera[0];

        float2 uv = ((float2(ipos) + 0.5f) / float2(isize)) * 2.0f - 1.0f;
        
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
