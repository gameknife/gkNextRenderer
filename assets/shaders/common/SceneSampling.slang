implementing PathTracing;

#include "PreProcessor.slang"
import Shader.VisibilityBuffer;

// Scene-space sampling helpers shared across the renderer: object-id encoding, page
// index, motion vectors, spherical-harmonic / IBL evaluation, G-buffer fetch and the
// screen-space trace fallback. Split out of the former Shading.slang grab-bag.

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
        // Some upscalers require moving-object vectors to be explicitly marked invalid.
        return Camera.UpscalerInvalidMotionMask && !Camera.RayReconstruction && MotionMoment[ipos] > 0
            ? float2(3.40282346638528859811704183484516925440e38f)
            : motion;
    }

    public float2 CalculateSkyMotionVector(in UniformBufferObject Camera, float3 worldRayDirection)
    {
        // Sky radiance is infinitely far away: camera translation must not contribute to
        // its motion, while camera rotation (and projection changes) still must reproject it.
        float4 currFrameHPos = mul(Camera.ViewProjectionUnJit, float4(worldRayDirection, 0));
        float4 prevFrameHPos = mul(Camera.PrevViewProjectionUnJit, float4(worldRayDirection, 0));
        float2 currfpos = currFrameHPos.xy / currFrameHPos.w * 0.5;
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

            int2 size = int2(Visibility.Dimensions(gpuScene.CustomData0));

            int2 sampleCoord = int2(currentUV * float2(size));
            uint2 vBufferSample = Visibility.Load(sampleCoord, gpuScene.CustomData0);
            if (!Visibility.IsValid(vBufferSample))
                continue;

            float4 origin = mul(Camera.ModelViewInverse, float4(0, 0, 0, 1));
            const float2 sampleNdc =
                ((float2(sampleCoord) + 0.5f) / float2(size)) * 2.0f - 1.0f;
            float4 target = mul(Camera.ProjectionInverse, float4(sampleNdc, 1, 1));
            float4 dir = mul(Camera.ModelViewInverse, float4(normalize(target.xyz), 0));
            float3 ray_dir = normalize(dir.xyz);

            Vertex sampleVertex;
            if (!Common.get_material_data(
                    vBufferSample, origin.xyz, ray_dir, NodeProxies, Models,
                    Vertices, PrimAddress, sampleVertex))
            {
                continue;
            }

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
