implementing PathTracing;

#include "PreProcessor.slang"
import Shader.VisibilityBuffer;
import Shader.SurfaceBuffer;

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

    // Shading-strategy flags for the Primary Surface (Shader/SurfaceBuffer.slang). Material
    // *identity* stays in the resolved material id; these say how the pixel has to be shaded, which
    // is what a scheduler buckets on. Kept next to the other G-buffer encoders so every writer of
    // RT_BSDF_DATA derives them the same way.
    public uint MakeSurfaceFlags(in Material material)
    {
        uint flags = 0u;
        if (material.MaterialModel == MaterialDiffuseLight)
        {
            flags |= Surface.kFlagEmissive;
        }
        if (material.MaterialModel == MaterialMetallic)
        {
            flags |= Surface.kFlagMetallic;
        }
        if (material.MaterialModel == MaterialDielectric)
        {
            flags |= Surface.kFlagDielectric;
        }
        if (material.MRATextureId >= 0)
        {
            flags |= Surface.kFlagHasMra;
        }
        if (material.NormalTextureId >= 0)
        {
            flags |= Surface.kFlagHasNormalMap;
        }
        return flags;
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

    // Surface-path screen-space trace. Same march, same acceptance test, but each step reads the
    // resolved surface instead of running a full get_material_data (three vertex fetches, a
    // barycentric solve and attribute interpolation) per step.
    bool TraceInScreenSpaceSurface(float3 position, float3 rayDir, float maxDistance,
                                   float depthTolerance, out float3 outPosition,
                                   out float3 outNormal, out uint outMaterialIdx)
    {
        UniformBufferObject Camera = Bindless.GetGpuscene().Camera[0];
        const uint viewBase = Bindless.GetGpuscene().CustomData0;

        const int maxSteps = 5;
        const float stepSize = maxDistance / float(maxSteps);
        const float3 rayStart = position + rayDir * 0.1;

        outPosition = float3(0, 0, 0);
        outNormal = float3(0, 0, 1);
        outMaterialIdx = 0;

        const int2 size = int2(Surface.Dimensions(viewBase));

        for (int i = 0; i < maxSteps; i++)
        {
            const float3 currentPos = rayStart + rayDir * (i * stepSize);
            float4 currentPosProj = mul(Camera.ViewProjection, float4(currentPos, 1.0));
            currentPosProj.xyz /= currentPosProj.w;
            const float2 currentUV = currentPosProj.xy * 0.5 + 0.5;
            if (any(currentUV < float2(0.0)) || any(currentUV > float2(1.0)))
                continue;

            const int2 sampleCoord = clamp(int2(currentUV * float2(size)), int2(0), size - 1);
            const Surface.FSurfaceGeometry sample = Surface.LoadGeometry(viewBase, sampleCoord);
            if (!Surface.IsValid(sample.Depth))
                continue;

            const float3 samplePosition = Surface.ReconstructPosition(
                Camera.ProjectionInverse, Camera.ModelViewInverse, sampleCoord, uint2(size),
                sample.Depth);
            const float viewSpaceCurrentDepth = mul(Camera.ModelView, float4(currentPos, 1.0)).z;
            const float viewSpaceSampleDepth = mul(Camera.ModelView, float4(samplePosition, 1.0)).z;

            if (viewSpaceCurrentDepth < viewSpaceSampleDepth &&
                viewSpaceCurrentDepth > viewSpaceSampleDepth - depthTolerance)
            {
                outPosition = samplePosition;
                outNormal = normalize(sample.Normal);
                outMaterialIdx = Surface.LoadMaterialId(viewBase, sampleCoord);
                return true;
            }
        }
        return false;
    }

    public bool TraceInScreenSpace(float3 position, float3 rayDir, float maxDistance, float depthTolerance,
                                   out float3 outPosition, out float3 outNormal, out uint outMaterialIdx)
    {
        if (Surface.IsPathActive(Bindless.GetGpuscene().CustomData2))
        {
            return TraceInScreenSpaceSurface(position, rayDir, maxDistance, depthTolerance,
                                             outPosition, outNormal, outMaterialIdx);
        }

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

        // Loop invariants: the visibility-buffer extent and the camera origin do not depend on the
        // step, and this march runs for every secondary ray in software tracing.
        const int2 size = int2(Visibility.Dimensions(gpuScene.CustomData0));
        const float4 origin = mul(Camera.ModelViewInverse, float4(0, 0, 0, 1));

        for (int i = 0; i < maxSteps; i++)
        {
            float3 currentPos = rayStart + rayDir * (i * stepSize);
            float4 currentPosProj = mul(Camera.ViewProjection, float4(currentPos, 1.0));
            currentPosProj.xyz /= currentPosProj.w;
            float2 currentUV = currentPosProj.xy * 0.5 + 0.5;
            if (any(currentUV < float2(0.0)) || any(currentUV > float2(1.0)))
                continue;

            int2 sampleCoord = clamp(int2(currentUV * float2(size)), int2(0), size - 1);
            uint2 vBufferSample = Visibility.Load(sampleCoord, gpuScene.CustomData0);
            if (!Visibility.IsValid(vBufferSample))
                continue;

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

    // Interleaved-gradient spatial hash, decorrelated per frame by the golden ratio so a temporal
    // resolve sees a genuinely new offset every frame. Core.GTAO deliberately drifts far slower
    // because it has a spatial filter to lean on; a dithered binary shadow has none, so its
    // sequence has to move fast enough for TAA to average it back out.
    float ScreenSpaceHash(uint2 pixel, uint frame)
    {
        const float seed = dot(float2(pixel), float2(0.06711056f, 0.00583715f));
        return frac(52.9829189f * frac(seed) + float(frame) * 0.6180339887f);
    }

    // World position and camera-facing geometric plane of the surface the visibility buffer holds
    // at a pixel. An occlusion query needs only those, so it must not pay for the attribute
    // interpolation get_material_data performs: the pixel's primary ray provably hits the stored
    // triangle, which makes a plane intersection exact and a barycentric solve redundant.
    bool LoadVisibilitySurfacePlane(int2 pixel, int2 size, out float3 worldPosition,
                                    out float3 worldNormal)
    {
        worldPosition = float3(0, 0, 0);
        worldNormal = float3(0, 0, 1);
        GPUScene scene = Bindless.GetGpuscene();
        const uint2 vBuffer = Visibility.Load(pixel, scene.CustomData0);
        if (!Visibility.IsValid(vBuffer))
        {
            return false;
        }

        UniformBufferObject Camera = scene.Camera[0];
        NodeProxy proxy = scene.Nodes[vBuffer.x - 1];
        ModelData model = scene.Offsets[proxy.modelId];
        float3 corners[3];
        [unroll]
        for (uint i = 0u; i < 3u; ++i)
        {
            const uint vertexIndex = scene.Indices[model.indexOffset + vBuffer.y * 3u + i];
            const uint vertexAddress = model.vertexOffset + vertexIndex;
            GPUVertex packedVertex;
            if (proxy.skinId != 0xFFFFFFFF)
            {
                packedVertex = scene.SkinnedVertices[vertexAddress];
            }
            else
            {
                packedVertex = scene.Vertices[vertexAddress];
            }
            const float3 local = float3(packedVertex.Position_Tx.xyz);
            corners[i] = mul(proxy.worldTS, float4(local, 1.0f)).xyz;
        }

        const float3 origin = mul(Camera.ModelViewInverse, float4(0, 0, 0, 1)).xyz;
        const float2 ndc = ((float2(pixel) + 0.5f) / float2(size)) * 2.0f - 1.0f;
        const float4 target = mul(Camera.ProjectionInverse, float4(ndc, 1.0f, 1.0f));
        const float3 rayDir = normalize(mul(Camera.ModelViewInverse, float4(normalize(target.xyz), 0.0f)).xyz);

        const float3 planeNormal = cross(corners[1] - corners[0], corners[2] - corners[0]);
        const float denominator = dot(planeNormal, rayDir);
        if (abs(denominator) <= 1e-12f)
        {
            return false;
        }
        const float t = dot(planeNormal, corners[0] - origin) / denominator;
        if (t <= 0.0f)
        {
            return false;
        }
        worldPosition = origin + rayDir * t;
        // Orient the plane toward the camera: the visibility buffer stores the face the camera
        // sees, and callers reason about what lies in front of or behind that face.
        worldNormal = normalize(planeNormal) * (denominator > 0.0f ? -1.0f : 1.0f);
        return true;
    }

    // Surface-path replacement for LoadVisibilitySurfacePlane. The Primary Surface already holds a
    // dense depth and normal, so a march step costs two texture reads instead of re-fetching three
    // vertices, re-skinning them, transforming them and intersecting the triangle plane.
    //
    // The plane it reports is the *shading* plane, not the exact geometric triangle plane: normal
    // maps and smooth vertex normals perturb it. For an occlusion approximation that is the right
    // trade (see the accuracy caveats on ScreenSpaceShadowVisibility); a caller that genuinely
    // needs the exact triangle plane must keep using LoadVisibilitySurfacePlane.
    bool LoadSurfaceShadingPlane(int2 pixel, int2 size, out float3 worldPosition,
                                 out float3 worldNormal)
    {
        worldPosition = float3(0, 0, 0);
        worldNormal = float3(0, 0, 1);
        UniformBufferObject Camera = Bindless.GetGpuscene().Camera[0];
        const Surface.FSurfaceGeometry geometry =
            Surface.LoadGeometry(Bindless.GetGpuscene().CustomData0, pixel);
        if (!Surface.IsValid(geometry.Depth))
        {
            return false;
        }
        const float normalLengthSquared = dot(geometry.Normal, geometry.Normal);
        if (normalLengthSquared < 1e-8f)
        {
            return false;
        }

        worldPosition = Surface.ReconstructPosition(
            Camera.ProjectionInverse, Camera.ModelViewInverse, pixel, uint2(size), geometry.Depth);
        // Orient toward the camera for the same reason the visibility plane does: callers reason
        // about what lies in front of or behind the face the camera can see.
        const float3 eye = mul(Camera.ModelViewInverse, float4(0, 0, 0, 1)).xyz;
        const float3 normal = geometry.Normal * rsqrt(normalLengthSquared);
        worldNormal = dot(normal, worldPosition - eye) > 0.0f ? -normal : normal;
        return true;
    }

    // Dispatch between the two: the Primary Surface when the pass declared it, the per-step
    // visibility decode otherwise. Renderers that have not migrated keep their exact old behaviour.
    bool LoadOccluderPlane(int2 pixel, int2 size, out float3 worldPosition, out float3 worldNormal)
    {
        if (Surface.IsPathActive(Bindless.GetGpuscene().CustomData2))
        {
            return LoadSurfaceShadingPlane(pixel, size, worldPosition, worldNormal);
        }
        return LoadVisibilitySurfacePlane(pixel, size, worldPosition, worldNormal);
    }

    // Coarse contact shadow between a shading point and a finite light, marched against the
    // visibility buffer. The visibility buffer only holds what the camera can see, so every
    // uncertain path here fails *open*: a missed occluder is a light leak that reads as soft
    // lighting, while a false occluder is a black blotch that reads as a bug.
    //
    // Known limits, inherent to screen-space data: occluders outside the frame or hidden behind
    // nearer geometry cast nothing, and the fixed step count cannot resolve an occluder thinner
    // than the local step. Callers must treat this as an approximation, never as a shadow test.
    public float ScreenSpaceShadowVisibility(float3 position, float3 normal, float3 lightPosition,
                                             float maxTraceDistance, uint2 screenPixel,
                                             bool temporalDither)
    {
        UniformBufferObject Camera = Bindless.GetGpuscene().Camera[0];
        const float3 toLight = lightPosition - position;
        const float lightDistance = length(toLight);
        if (lightDistance <= EPS || maxTraceDistance <= EPS)
        {
            return 1.0f;
        }
        const float3 wi = toLight / lightDistance;

        // The emitter's own geometry is in the visibility buffer. Marching all the way to it makes
        // every light shadow the very surfaces it illuminates, so stop short of the emitter.
        const float traceLength = min(maxTraceDistance, lightDistance) * 0.95f;
        if (traceLength <= EPS)
        {
            return 1.0f;
        }

        const float receiverViewZ = mul(Camera.ModelView, float4(position, 1.0f)).z;
        // Biases scale with view depth so they stay a roughly constant number of pixels instead of
        // a constant world distance, which is what actually governs reconstruction error here.
        const float depthScale = max(abs(receiverViewZ), 1.0f);
        const float planeBias = 0.004f * depthScale;
        const float3 rayOrigin = position + normal * planeBias;
        const float3 rayEnd = rayOrigin + wi * traceLength;

        const float4 clipStart = mul(Camera.ViewProjection, float4(rayOrigin, 1.0f));
        const float4 clipEnd = mul(Camera.ViewProjection, float4(rayEnd, 1.0f));
        // A shading point behind the near plane has no screen-space neighbourhood to march.
        const float minClipW = 0.05f;
        if (clipStart.w <= minClipW)
        {
            return 1.0f;
        }
        // A light toward the camera pushes the far end behind the near plane; clip the segment
        // there rather than dropping the shadow, since w is affine along the world-space segment.
        float endFraction = 1.0f;
        if (clipEnd.w < minClipW)
        {
            endFraction = saturate((minClipW - clipStart.w) / (clipEnd.w - clipStart.w));
        }
        if (endFraction <= 0.01f)
        {
            return 1.0f;
        }

        const int2 size = int2(Visibility.Dimensions(gpuScene.CustomData0));
        const float dither = ScreenSpaceHash(screenPixel, temporalDither ? Camera.TotalFrames : 0u);

        // Short projected segments do not benefit from twelve visibility lookups. Scale the march
        // budget in screen space so distant/small lights use 4 steps, medium segments 8, and only
        // long rays pay the original 12-step cost.
        const float4 clippedEnd = lerp(clipStart, clipEnd, endFraction);
        const float2 startPixel = (clipStart.xy / clipStart.w) * (0.5f * float2(size));
        const float2 endPixel = (clippedEnd.xy / clippedEnd.w) * (0.5f * float2(size));
        const float projectedLength = length(endPixel - startPixel);
        const uint maximumSteps = clamp(Camera.LightObjectShadowSteps, 4u, 12u);
        const uint stepCount = projectedLength <= 24.0f
            ? min(4u, maximumSteps)
            : (projectedLength <= 96.0f ? min(8u, maximumSteps) : maximumSteps);
        for (uint i = 0u; i < stepCount; ++i)
        {
            // Quadratic spacing concentrates the budget near the receiver, where contact shadows
            // carry the contact cue and where a missed step is most visible.
            const float s = (float(i) + dither) / float(stepCount);
            const float fraction = endFraction * s * s;

            const float4 clip = lerp(clipStart, clipEnd, fraction);
            if (clip.w < minClipW)
            {
                break;
            }
            const float2 uv = (clip.xy / clip.w) * 0.5f + 0.5f;
            if (any(uv < float2(0.0f)) || any(uv >= float2(1.0f)))
            {
                // The ray left the frame; nothing further along it is knowable.
                break;
            }

            float3 occluderPosition;
            float3 occluderNormal;
            const int2 sampleCoord = clamp(int2(uv * float2(size)), int2(0), size - 1);
            if (!LoadOccluderPlane(sampleCoord, size, occluderPosition, occluderNormal))
            {
                continue;
            }

            // Every point of a shadow ray leaving a lit surface lies above that surface's tangent
            // plane, so anything at or below the plane cannot occlude it. Skipping those is exact,
            // not a bias hack, and it is what keeps a flat surface from shadowing itself: under
            // grazing light the ray rides its own plane, where any depth comparison there is pure
            // reconstruction noise and dithers into a stipple pattern.
            if (dot(occluderPosition - position, normal) <= planeBias)
            {
                continue;
            }

            // Measure against the occluder's own plane rather than comparing view depths at the
            // sampled texel. Both describe the same geometry, but a texel of a surface seen
            // near edge-on spans a large depth range, so a depth comparison there is dominated by
            // texel slope - which is what banded the wall/ceiling junction. The plane distance
            // has no such error. The thickness bound remains a guess: the visibility buffer
            // records a front face, never the depth of the solid behind it.
            const float3 rayPosition = lerp(rayOrigin, rayEnd, fraction);
            const float behindSurface = -dot(occluderNormal, rayPosition - occluderPosition);
            const float thickness = max(0.2f, depthScale * 0.03f);
            if (behindSurface > planeBias && behindSurface < thickness)
            {
                // Fade a hit found near the frame border: the occluder is about to scroll out of
                // the visibility buffer, and a hard cutoff there pops as the camera turns.
                const float2 borderDistance = min(uv, 1.0f - uv);
                return 1.0f - saturate(min(borderDistance.x, borderDistance.y) / 0.08f);
            }
        }
        return 1.0f;
    }
}
