import Shader.Bindless;
module SurfaceBuffer;

// The Primary Surface is the single dense, already-resolved description of what the camera sees at
// a pixel: geometry, material parameters and motion, produced once by Core.SurfaceBuild and read by
// every downstream consumer. It deliberately reuses the existing RT bank rather than adding a
// parallel resource set, so this module is a *codec*, not a storage owner: it is the only place
// that knows which RT slot carries which semantic, what the background sentinel is, and how the
// BSDF word is packed. Business shaders must go through it and never hand-assemble slot
// combinations, exactly as they must go through Visibility.Load for the visibility buffer.
//
// The C++ mirror of this contract lives in Rendering/PipelineCommon/SurfaceBufferLayout.hpp.
namespace Surface
{
    // --- Contract constants -------------------------------------------------------------------

    // Depth written where the primary ray hit nothing. Deliberately outside [0,1] so any consumer
    // that only asks "is this a surface?" gets the right answer without a second buffer.
    public static const float kBackgroundDepth = 1000.0f;

    // RT_BSDF_DATA.x for background / degenerate samples. A resolved material id is a real index
    // into GPUScene materials, so it can never collide with this.
    public static const uint kInvalidMaterialId = 0xFFFFFFFFu;

    // Feature flags, packed into the high 16 bits of RT_BSDF_DATA.y. Material *identity* stays in
    // the material id; these describe the shading *strategy*, which is what a scheduler buckets on.
    // Keep this set minimal - it is not a place to mirror the material system.
    public static const uint kFlagEmissive = 1u << 0u;
    public static const uint kFlagMetallic = 1u << 1u;
    public static const uint kFlagDielectric = 1u << 2u;
    public static const uint kFlagHasMra = 1u << 3u;
    public static const uint kFlagHasNormalMap = 1u << 4u;

    // GPUScene.CustomData2 bit selecting the surface path. Bit 0 is the checkerboard flag
    // (common/CheckerboardRendering.slang); both are pass-local and travel in the same word.
    public static const uint kPathFlag = 1u << 1u;

    // GPUScene.CustomData2 bit: shading allocation comes from the tile classification
    // (Shader/ShadingScheduler.slang) rather than the analytic full-screen mapping. Implies kPathFlag.
    public static const uint kSchedulerFlag = 1u << 2u;

    // GPUScene.CustomData2 bit: the shading kernel upsamples GTAO and folds it into the ambient
    // term itself, instead of leaving that to the compose pass. Implies kPathFlag, because it only
    // works when GTAO runs before shading.
    public static const uint kGtaoInCoreFlag = 1u << 3u;

    // GPUScene.CustomData1 bits for Core.SurfaceBuild: which optional planes this build writes.
    // Specular albedo is material data that only the tracing renderers consume, so a light
    // consumer (SoftwareModernNoAmbient) does not pay for it.
    public static const uint kBuildWriteSpecularAlbedo = 1u << 0u;

    public bool IsPathActive(uint customData2)
    {
        return (customData2 & kPathFlag) != 0u;
    }

    public bool IsSchedulerActive(uint customData2)
    {
        return (customData2 & kSchedulerFlag) != 0u;
    }

    public bool IsGtaoAppliedInCore(uint customData2)
    {
        return (customData2 & kGtaoInCoreFlag) != 0u;
    }

    // --- Payloads -----------------------------------------------------------------------------

    // Everything Build resolves. Consumers rarely want all of it; the narrower loads below exist so
    // a screen-space march does not read eight planes to answer a depth question.
    public struct FPrimarySurface
    {
        public float Depth;           // NDC depth (jittered ViewProjection), kBackgroundDepth = miss
        public float3 Normal;         // world shading normal, normal-mapped
        public float Roughness;       // perceptual roughness, MRA already applied
        public float4 Albedo;         // base color, diffuse texture already multiplied in
        public float4 SpecularAlbedo; // specular albedo (base color for metals)
        public uint ObjectId;         // Common.EncodeObjectId payload
        public float2 Motion;         // render-pixel units
        public uint MaterialId;       // resolved GPUScene material index, kInvalidMaterialId = none
        public float Metalness;       // effective metalness, MRA and material-model override applied
        public uint Flags;            // kFlag* bitset
    };

    // Geometry-only view, for screen-space marching (occlusion, contact shadows, SS tracing).
    public struct FSurfaceGeometry
    {
        public float Depth;
        public float3 Normal;
        public float Roughness;
    };

    // Everything a shading kernel needs; skips object id, motion and specular albedo.
    public struct FSurfaceShading
    {
        public float Depth;
        public float3 Normal;
        public float Roughness;
        public float4 Albedo;
        public uint MaterialId;
        public float Metalness;
        public uint Flags;
    };

    // --- BSDF word packing --------------------------------------------------------------------

    public uint PackBsdfWord(float metalness, uint flags)
    {
        return (uint(f32tof16(saturate(metalness))) & 0xFFFFu) | ((flags & 0xFFFFu) << 16u);
    }

    public float UnpackMetalness(uint bsdfWord)
    {
        return f16tof32(bsdfWord & 0xFFFFu);
    }

    public uint UnpackFlags(uint bsdfWord)
    {
        return (bsdfWord >> 16u) & 0xFFFFu;
    }

    public bool HasFlag(uint flags, uint flag)
    {
        return (flags & flag) != 0u;
    }

    // --- Validity and reconstruction ----------------------------------------------------------

    public bool IsValid(float depth)
    {
        return depth >= 0.0f && depth <= 1.0f;
    }

    // Camera-space position of the sample. Build stores depth through the *jittered* view
    // projection, so reconstruction must invert the jittered projection: pass Camera.ProjectionInverse
    // (not the UnJit variant) or the reconstructed point drifts by the jitter offset.
    public float3 ReconstructViewPosition(
        float4x4 projectionInverse, int2 pixel, uint2 size, float depth)
    {
        const float2 ndc = ((float2(pixel) + 0.5f) / float2(size)) * 2.0f - 1.0f;
        const float4 view = mul(projectionInverse, float4(ndc, depth, 1.0f));
        const float safeW = abs(view.w) > 1e-6f ? view.w : (view.w >= 0.0f ? 1e-6f : -1e-6f);
        return view.xyz / safeW;
    }

    public float3 ReconstructPosition(
        float4x4 projectionInverse, float4x4 modelViewInverse, int2 pixel, uint2 size, float depth)
    {
        const float3 view = ReconstructViewPosition(projectionInverse, pixel, size, depth);
        return mul(modelViewInverse, float4(view, 1.0f)).xyz;
    }

    // --- Physical access ----------------------------------------------------------------------
    // The only code in the engine allowed to name a surface RT slot.

    RWTexture2D<float> DepthPlane(uint viewBase)
    {
        return Bindless.GetStorageTexture<float>(
            Bindless.ViewRT(viewBase, Bindless.RT_PREV_DEPTHBUFFER));
    }

    RWTexture2D<float4> NormalPlane(uint viewBase)
    {
        return Bindless.GetStorageTexture<float4>(Bindless.ViewRT(viewBase, Bindless.RT_NORMAL));
    }

    RWTexture2D<float4> AlbedoPlane(uint viewBase)
    {
        return Bindless.GetStorageTexture<float4>(Bindless.ViewRT(viewBase, Bindless.RT_ALBEDO));
    }

    RWTexture2D<float4> SpecularAlbedoPlane(uint viewBase)
    {
        return Bindless.GetStorageTexture<float4>(
            Bindless.ViewRT(viewBase, Bindless.RT_SPECULAR_ALBEDO));
    }

    RWTexture2D<uint> ObjectIdPlane(uint viewBase)
    {
        return Bindless.GetStorageTexture<uint>(Bindless.ViewRT(viewBase, Bindless.RT_OBJECTID_0));
    }

    RWTexture2D<float4> MotionPlane(uint viewBase)
    {
        return Bindless.GetStorageTexture<float4>(
            Bindless.ViewRT(viewBase, Bindless.RT_MOTIONVECTOR));
    }

    RWTexture2D<uint2> BsdfPlane(uint viewBase)
    {
        return Bindless.GetStorageTexture<uint2>(Bindless.ViewRT(viewBase, Bindless.RT_BSDF_DATA));
    }

    public uint2 Dimensions(uint viewBase)
    {
        return Bindless.GetStorageTextureDimensions<float>(
            Bindless.ViewRT(viewBase, Bindless.RT_PREV_DEPTHBUFFER));
    }

    // --- Store / Load -------------------------------------------------------------------------

    public void Store(uint viewBase, int2 pixel, FPrimarySurface s, bool writeSpecularAlbedo)
    {
        DepthPlane(viewBase)[pixel] = s.Depth;
        NormalPlane(viewBase)[pixel] = float4(s.Normal, s.Roughness);
        AlbedoPlane(viewBase)[pixel] = s.Albedo;
        ObjectIdPlane(viewBase)[pixel] = s.ObjectId;
        MotionPlane(viewBase)[pixel] = float4(s.Motion, 0.0f, 0.0f);
        BsdfPlane(viewBase)[pixel] = uint2(s.MaterialId, PackBsdfWord(s.Metalness, s.Flags));
        if (writeSpecularAlbedo)
        {
            SpecularAlbedoPlane(viewBase)[pixel] = s.SpecularAlbedo;
        }
    }

    public FPrimarySurface Load(uint viewBase, int2 pixel)
    {
        const float4 normal = NormalPlane(viewBase)[pixel];
        const uint2 bsdf = BsdfPlane(viewBase)[pixel];
        const float4 motion = MotionPlane(viewBase)[pixel];

        FPrimarySurface s;
        s.Depth = DepthPlane(viewBase)[pixel];
        s.Normal = normal.xyz;
        s.Roughness = normal.w;
        s.Albedo = AlbedoPlane(viewBase)[pixel];
        s.SpecularAlbedo = SpecularAlbedoPlane(viewBase)[pixel];
        s.ObjectId = ObjectIdPlane(viewBase)[pixel];
        s.Motion = motion.xy;
        s.MaterialId = bsdf.x;
        s.Metalness = UnpackMetalness(bsdf.y);
        s.Flags = UnpackFlags(bsdf.y);
        return s;
    }

    public FSurfaceShading LoadShading(uint viewBase, int2 pixel)
    {
        const float4 normal = NormalPlane(viewBase)[pixel];
        const uint2 bsdf = BsdfPlane(viewBase)[pixel];

        FSurfaceShading s;
        s.Depth = DepthPlane(viewBase)[pixel];
        s.Normal = normal.xyz;
        s.Roughness = normal.w;
        s.Albedo = AlbedoPlane(viewBase)[pixel];
        s.MaterialId = bsdf.x;
        s.Metalness = UnpackMetalness(bsdf.y);
        s.Flags = UnpackFlags(bsdf.y);
        return s;
    }

    public FSurfaceGeometry LoadGeometry(uint viewBase, int2 pixel)
    {
        const float4 normal = NormalPlane(viewBase)[pixel];

        FSurfaceGeometry s;
        s.Depth = DepthPlane(viewBase)[pixel];
        s.Normal = normal.xyz;
        s.Roughness = normal.w;
        return s;
    }

    public float LoadDepth(uint viewBase, int2 pixel)
    {
        return DepthPlane(viewBase)[pixel];
    }

    public uint LoadMaterialId(uint viewBase, int2 pixel)
    {
        return BsdfPlane(viewBase)[pixel].x;
    }

    // Raw material word, for consumers that only need identity and strategy - tile classification
    // buckets on exactly this and must not pay for the geometry planes.
    public uint2 LoadBsdf(uint viewBase, int2 pixel)
    {
        return BsdfPlane(viewBase)[pixel];
    }

    public float4 LoadSpecularAlbedo(uint viewBase, int2 pixel)
    {
        return SpecularAlbedoPlane(viewBase)[pixel];
    }

    public float4 LoadAlbedo(uint viewBase, int2 pixel)
    {
        return AlbedoPlane(viewBase)[pixel];
    }
}
