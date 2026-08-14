implementing PathTracing;

#include "PreProcessor.slang"

// FPathTracingRenderer: the multi-bounce path tracer. Drives any IRayTracer +
// IDirectIlluminator + IRadianceCache. Direct-lighting strategies and the sun/light
// NEE helpers live in Lighting.slang; IRayTracer impls in RayTracers.slang.

// Outcome of scattering off the current surface and tracing the next path segment.
// The scattered direction, hit vertex, throughput and random seed are threaded through
// ScatterAndTrace as inout state; everything the caller needs to decide is returned here.
struct FBounceResult
{
    bool exited;             // segment terminates here (sky miss, emitter, or back-face)
    bool hitMiss;            // escaped the scene into the sky
    bool hitReflect;         // scattered through a specular/GGX lobe (vs diffuse hemisphere)
    bool hitMetal;           // the metallic lobe was chosen
    float hitDist;           // traced segment length
    float3 terminalRadiance; // sky / emissive radiance when the segment terminates

    __init()
    {
        exited = false;
        hitMiss = false;
        hitReflect = false;
        hitMetal = false;
        hitDist = 0.0f;
        terminalRadiance = float3(0, 0, 0);
    }
};

// Secondary (post-first-bounce) direct-lighting strategy selector (FPathTracingRenderer.SecondaryDirectMode).
static const uint kSecondaryDirectPlainSun = 0u;       // naive PT 50% random sun next-event estimation
static const uint kSecondaryDirectTerminateIfLit = 1u; // terminate once the path sees direct light (cache = indirect)
static const uint kSecondaryDirectNone = 2u;           // no secondary direct (already fed to the cache)

public struct FPathTracingRenderer{
    public float ExitProbability = 0.5;
    public bool ExitAfterFirst = false;
    public float HitNormalOffset = 0.0f;
    public int SampleDownscale = 1;
    public bool FuzzyTracing = false;
    public bool ForceExitAfterFirst = false;
    public bool EnableSoftTracingAdaptiveOffset = false;
    public bool UseAmbientCubeTerminal = true;
    public bool WriteOutputs = true;
    // Secondary (post-first-bounce) direct lighting strategy, decoupled from any cache:
    //   0 = PlainSunNEE          : naive PT 50% random sun next-event estimation
    //   1 = DirectTerminateIfLit : terminate the path once it sees direct light (cache provides indirect)
    //   2 = None                 : no secondary direct lighting (direct already fed to the cache)
    public uint SecondaryDirectMode = 0;

    [mutating]
    public FDirectLighting EvaluateDirectLighting(IDirectIlluminator directIllum, Vertex vertex,
                                                  float3 incomingRayDirection)
    {
        Material material = Bindless.GetGpuscene().GetMaterial(vertex.MaterialIndex);
        FBSDFContext context = BuildBSDFContext(vertex, material, incomingRayDirection);
        FDirectLighting direct = FDirectLighting();
        directIllum.DirectIlluminate(RandomSeed_, context, direct);
        direct.DiffuseDemodulated = max(direct.DiffuseDemodulated, float3(0, 0, 0));
        direct.SpecularRadiance = max(direct.SpecularRadiance, float3(0, 0, 0));
        return direct;
    }

    // Radiance applied when a path is forced to terminate on a surface.
    // Returns the terminal radiance; the caller multiplies it into RayColor and,
    // when ambient-cube terminal is active, feeds it to the cache as a miss.
    float4 ApplyTerminalRadiance(Vertex vertex)
    {
        if (UseAmbientCubeTerminal)
        {
            float4 terminal = interpolateAmbientCubes<FullAmbientCubeSampler>(vertex.Position, vertex.Normal);
            // The terminal is the path's remaining indirect energy, so it takes the same artistic
            // multiplier as a SHARC cache hit (see FSharcQueryCache.TryQueryRadiance).
            terminal.rgb *= Bindless.GetGpuscene().Camera[0].IndirectIntensity;
            return terminal;
        }
        return float4(0, 0, 0, 1);
    }

    // Terminate a path on the local ambient cube: fold the terminal radiance into the
    // throughput and, when the ambient-cube terminal is active, feed it to the cache as a miss.
    void ApplyAmbientTerminal<TCache : IRadianceCache>(inout TCache cache, Vertex vertex,
                                                       inout float4 rayColor)
    {
        const float4 terminalRadiance = ApplyTerminalRadiance(vertex);
        rayColor *= terminalRadiance;
        if (UseAmbientCubeTerminal)
        {
            cache.OnMiss(terminalRadiance.rgb);
        }
    }

    // Record a path vertex with the cache, evaluating its direct lighting only when the cache
    // asks for it (SHARC update). Single source for the primary-vertex and per-bounce cache
    // recording; returns whether the path should continue.
    [mutating]
    bool RecordSurfaceHit<TCache : IRadianceCache>(IDirectIlluminator directIllum, inout TCache cache,
                                                   bool wantsDirect, Vertex vertex,
                                                   float3 incomingRayDirection)
    {
        const float3 direct = wantsDirect
            ? EvaluateDirectLighting(directIllum, vertex, incomingRayDirection).DiffuseDemodulated
            : float3(0, 0, 0);
        return cache.OnSurfaceHit(vertex.Position, vertex.Normal, direct, RandomSeed_);
    }

    // Does this light's surface contain the point, for this material index? Area emitters test
    // point-on-quad; point emitters test their radius. includePoint selects whether analytic point
    // lights participate (area-only vs any emitter).
    bool MatchesRegisteredEmitter(uint lightIndex, float3 position, uint materialIndex,
                                  bool includePoint, bool areaOnly)
    {
        LightObject light = Bindless.GetGpuscene().Lights[lightIndex];
        if (light.lightMatIdx != materialIndex)
        {
            return false;
        }
        if (light.lightType == LightTypeArea)
        {
            return IsPointOnAreaLight(position, light, EPS10);
        }
        return !areaOnly && includePoint && light.lightType == LightTypePoint &&
               light.p0.w > 0.0f && distance(position, light.p0.xyz) <= light.p0.w + EPS10;
    }

    bool FindRegisteredEmitterIn(Common.FLightCell cell, float3 position, uint materialIndex,
                                 bool includePoint, bool areaOnly, out uint outLightIndex)
    {
        outLightIndex = 0u;
        for (uint slot = 0u; slot < cell.Count; ++slot)
        {
            const uint lightIndex = cell.GetLightIndex(slot);
            if (MatchesRegisteredEmitter(lightIndex, position, materialIndex, includePoint, areaOnly))
            {
                outLightIndex = lightIndex;
                return true;
            }
        }
        return false;
    }

    // Which registered emitter does this point sit on? Identification is deliberately independent
    // of whether light sampling would pick that emitter anywhere: when the grid culls a light, the
    // BSDF strategy carries all of its energy at MIS weight one, so failing to recognise it here
    // would delete the light from the image entirely.
    //
    // The hit point's own cell answers virtually every query - a light is all but guaranteed to be
    // in the cell its surface occupies - and the full-list scan is the rare correctness backstop.
    bool FindRegisteredEmitter(float3 position, uint materialIndex, bool includePoint,
                               bool areaOnly, out uint outLightIndex)
    {
        const Common.FLightCell cell = Common.GetLightCell(position);
        if (FindRegisteredEmitterIn(cell, position, materialIndex, includePoint, areaOnly,
                                    outLightIndex))
        {
            return true;
        }
        if (cell.UsesGlobalFallback)
        {
            return false;
        }
        return FindRegisteredEmitterIn(Common.GetGlobalLightCell(), position, materialIndex,
                                       includePoint, areaOnly, outLightIndex);
    }

    bool IsRegisteredEmitterHit(float3 position, uint materialIndex, bool includePoint)
    {
        uint lightIndex;
        return FindRegisteredEmitter(position, materialIndex, includePoint, false, lightIndex);
    }

    bool GetRegisteredAreaLightPdf(float3 position, uint materialIndex, float3 shadingPosition,
                                   out float pdfOmega)
    {
        pdfOmega = 0.0f;
        uint lightIndex;
        if (!FindRegisteredEmitter(position, materialIndex, false, true, lightIndex))
        {
            return false;
        }

        LightObject light = Bindless.GetGpuscene().Lights[lightIndex];
        const float3 toLight = position - shadingPosition;
        const float distanceSquared = dot(toLight, toLight);
        const float lightCosine =
            max(dot(normalize(light.normal_area.xyz), -normalize(toLight)), 0.0f);
        const float area = light.normal_area.w;
        // Two different positions, and swapping them is silent: the emitter is identified where the
        // ray landed, but the pdf must be the one light sampling would have used *from the shading
        // point*, because that is the strategy this MIS weight is balancing against.
        const float lightSelectionPdf = Common.GetLightSelectionPdf(lightIndex, shadingPosition);
        if (distanceSquared <= EPS * EPS || lightCosine <= 0.0f || area <= 0.0f ||
            lightSelectionPdf <= 0.0f)
        {
            // A culled light reports no light-sampling density, so the caller's PowerHeuristic
            // collapses to one and the BSDF strategy takes the whole contribution. Returning true
            // with pdfOmega zero is what keeps that light in the image.
            return distanceSquared > EPS * EPS && lightCosine > 0.0f && area > 0.0f;
        }
        pdfOmega = (lightSelectionPdf / area) * distanceSquared / lightCosine;
        return true;
    }

    float3 FetchEmitterRadiance(Vertex vertex)
    {
        Material material = Bindless.GetGpuscene().GetMaterial(vertex.MaterialIndex);
        if (material.MaterialModel != MaterialDiffuseLight)
        {
            return float3(0, 0, 0);
        }
        float3 radiance = material.Diffuse.rgb;
        if (material.DiffuseTextureId >= 0)
        {
            radiance *= Bindless.GetSampleTexture(material.DiffuseTextureId)
                                .SampleLevel(vertex.TexCoord, 0).rgb;
        }
        return radiance;
    }

    // Glossy direct-light proposal. Near-delta reflection is deterministic; rough GGX uses
    // a normalized proposal with MIS against area/sun light sampling.
    float3 EvaluateGlossyDirect(IRayTracer tracer, FBSDFContext context, Vertex sourceVertex,
                                float normalOffset, inout uint4 randomSeed)
    {
        if (context.MaterialModel != MaterialMixture &&
            context.MaterialModel != MaterialMetallic &&
            context.MaterialModel != MaterialDielectric)
        {
            return float3(0, 0, 0);
        }

        const float3 incoming = -context.Wo;
        const float NoV = max(dot(context.Normal, context.Wo), 0.0f);
        const float3 fresnel = BSDFFresnelSchlick(NoV, BSDFMaterialF0(context));
        float3 result = float3(0, 0, 0);

        const bool isDelta = context.PerceptualRoughness <= BSDF_DELTA_ROUGHNESS;
        float proposalPdf = 0.0f;
        const float3 reflected = isDelta
            ? normalize(reflect(incoming, context.Normal))
            : SampleGlossyProposal(randomSeed, context, proposalPdf);
        if (dot(reflected, context.Normal) <= 0.0f)
        {
            return float3(0, 0, 0);
        }
        Vertex reflectedHit = sourceVertex;
        NodeProxy reflectedNode;
        float4 traceColor = float4(1, 1, 1, 1);
        float hitDistance = 0.0f;
        const float3 reflectionOrigin = context.Position + context.Normal * max(normalOffset, EPS2);
        if (tracer.TraceRay(reflectionOrigin, reflected, PT_MAX_TRACE_DISTANCE, reflectedHit,
                            reflectedNode, traceColor, hitDistance))
        {
            float lightPdf = 0.0f;
            if (GetRegisteredAreaLightPdf(reflectedHit.Position, reflectedHit.MaterialIndex,
                                          context.Position, lightPdf))
            {
                if (isDelta)
                {
                    result += fresnel * FetchEmitterRadiance(reflectedHit);
                }
                else if (proposalPdf > 0.0f)
                {
                    FBSDFEval bsdf = EvaluateSurfaceBSDF(context, reflected);
                    const float NoL = max(dot(context.Normal, reflected), 0.0f);
                    const float mis = PowerHeuristic(proposalPdf, lightPdf);
                    result += FetchEmitterRadiance(reflectedHit) * bsdf.Specular *
                              (NoL * mis / proposalPdf);
                }
            }
        }
        else
        {
            const float3 sunRadiance = Common.EvaluateAnalyticSunDisk(reflected);
            if (isDelta)
            {
                result += fresnel * sunRadiance;
            }
            else if (proposalPdf > 0.0f && any(sunRadiance > float3(0, 0, 0)))
            {
                FBSDFEval bsdf = EvaluateSurfaceBSDF(context, reflected);
                const float NoL = max(dot(context.Normal, reflected), 0.0f);
                const float mis = PowerHeuristic(proposalPdf, Common.SunSamplePdf());
                result += sunRadiance * bsdf.Specular * (NoL * mis / proposalPdf);
            }
        }

        if (isDelta && context.MaterialModel == MaterialDielectric)
        {
            const float eta = context.FrontFace
                ? context.EtaOutside / context.EtaInside
                : context.EtaInside / context.EtaOutside;
            const float3 transmitted = refract(incoming, context.Normal, eta);
            if (dot(transmitted, transmitted) > 1e-8f)
            {
                Vertex transmittedHit = sourceVertex;
                NodeProxy transmittedNode;
                traceColor = float4(1, 1, 1, 1);
                hitDistance = 0.0f;
                const float3 transmissionOrigin =
                    context.Position - context.Normal * max(normalOffset, EPS2);
                if (tracer.TraceRay(transmissionOrigin, normalize(transmitted), PT_MAX_TRACE_DISTANCE,
                                    transmittedHit, transmittedNode, traceColor, hitDistance))
                {
                    if (IsRegisteredEmitterHit(transmittedHit.Position,
                                               transmittedHit.MaterialIndex, false))
                    {
                        result += (1.0f - fresnel) * FetchEmitterRadiance(transmittedHit);
                    }
                }
                else
                {
                    result += (1.0f - fresnel) *
                              Common.EvaluateAnalyticSunDisk(normalize(transmitted));
                }
            }
        }
        return result;
    }

    // Scatter off the current surface (choosing a diffuse / GGX / metal / dielectric lobe)
    // and trace the next segment. inVertex/rayDir/rayColor/randomSeed are threaded inout;
    // the bounce outcome is returned. RNG draw order defines the sampling and must be kept.
    FBounceResult ScatterAndTrace(IRayTracer tracer, inout Vertex inVertex, inout float3 rayDir,
                                  inout float4 rayColor, inout uint4 randomSeed,
                                  bool suppressDiffuseEmitter, bool suppressSpecularEmitter)
    {
        UniformBufferObject Camera = Bindless.GetGpuscene().Camera[0];
        FBounceResult result = FBounceResult();

        Material mat = Bindless.GetGpuscene().GetMaterial(inVertex.MaterialIndex);
        const float startPosOffset = mat.MaterialModel == MaterialDielectric ? 0.0f : 1.0f;
        float roughness = mat.Fuzziness;
        float metalness = mat.Metalness;
        if (mat.MRATextureId >= 0)
        {
            float4 mra = Bindless.GetSampleTexture(mat.MRATextureId).SampleLevel(inVertex.TexCoord, 0);
            roughness = roughness * mra.g;
            metalness = metalness * mra.b;
        }

        // --- Lobe selection + outgoing direction (RNG order is load-bearing) ---
        const float dotValue = dot(rayDir, inVertex.Normal);
        const bool backFace = dotValue > 0;
        const float3 outwardNormal = backFace ? -inVertex.Normal : inVertex.Normal;
        const float niOverNt = backFace ? mat.RefractionIndex2 : (1 / mat.RefractionIndex2);
        const float cosine = dotValue > 0 ? mat.RefractionIndex * dotValue : -dotValue;
        const float reflectProb = Schlick(cosine, mat.RefractionIndex);
        const float cosTheta = min(dot(-rayDir, outwardNormal), 1.0f);
        const float sinTheta = sqrt(max(0.0f, 1.0f - cosTheta * cosTheta));
        const bool cannotRefract = niOverNt * sinTheta > 1.0f;

        const bool chanceReflect = RandomFloat(randomSeed) < reflectProb;
        const bool chanceMetal = RandomFloat(randomSeed) < metalness;
        const bool chanceGGX = chanceReflect || chanceMetal;
        const float3 traceNext = chanceGGX ? reflect(rayDir, outwardNormal) : outwardNormal;
        float3 traceDir = chanceGGX ? ggxSampling(randomSeed, sqrt(roughness), traceNext)
                                    : AlignWithNormal(RandomInHemiSphere1(randomSeed), traceNext);
        result.hitReflect = chanceGGX;
        result.hitMetal = chanceMetal;

        if (mat.MaterialModel == MaterialDielectric && !chanceReflect)
        {
            if (cannotRefract)
            {
                traceDir = ggxSampling(randomSeed, sqrt(roughness), reflect(rayDir, outwardNormal));
                result.hitReflect = true;
            }
            else
            {
                traceDir = refract(rayDir, outwardNormal, niOverNt);
            }
        }
        rayDir = traceDir;

        // --- Trace the segment ---
        NodeProxy hitNode;
        float4 tempColor = float4(0, 0, 0, 1);
        float surfaceOffset = EnableSoftTracingAdaptiveOffset
            ? max(HitNormalOffset, GetAmbientCubeUnitAtPosition(inVertex.Position) * 0.5f)
            : HitNormalOffset;
        const float3 traceOrigin = inVertex.Position + inVertex.Normal * surfaceOffset * startPosOffset;
        float hitDist = 0.0f;
        if (!tracer.TraceRay(traceOrigin, traceDir, PT_MAX_TRACE_DISTANCE, inVertex, hitNode,
                             tempColor, hitDist))
        {
            // Miss: escaped into the sky.
            const float4 skyColor = Common.SampleSkyRadiance(traceDir, 0.0f);
            rayColor *= skyColor;
            result.hitMiss = true;
            result.terminalRadiance = skyColor.rgb;
            result.hitDist = PT_MAX_TRACE_DISTANCE;
            result.exited = true;
            return result;
        }
        result.hitDist = hitDist;

        // Fuzzy tracing short-circuits: the tracer already folded a color into tempColor.
        if (FuzzyTracing)
        {
            rayColor *= tempColor;
            return result;
        }

        // --- Classify the hit surface ---
        Material hitMat = Bindless.GetGpuscene().GetMaterial(inVertex.MaterialIndex);
        float4 hitAlbedo = hitMat.Diffuse;
        if (hitMat.DiffuseTextureId >= 0)
        {
            hitAlbedo *= Bindless.GetSampleTexture(hitMat.DiffuseTextureId).SampleLevel(inVertex.TexCoord, 0);
        }
        if (hitMat.MaterialModel == MaterialDiffuseLight || !chanceReflect)
        {
            rayColor *= hitAlbedo;
        }
        if (backFace && mat.MaterialModel != MaterialDielectric)
        {
            rayColor = float4(0.5f, 0.5f, 0.5f, 0);
            result.exited = true;
            return result;
        }
        if (hitMat.MaterialModel == MaterialDiffuseLight)
        {
            const bool dielectricTransmission = mat.MaterialModel == MaterialDielectric && !chanceGGX;
            const bool coveredByDirectEstimator = chanceGGX
                ? suppressSpecularEmitter
                : (suppressDiffuseEmitter && !dielectricTransmission);
            if (coveredByDirectEstimator &&
                IsRegisteredEmitterHit(inVertex.Position, inVertex.MaterialIndex, true))
            {
                // The same primary diffuse-light path is already represented by light NEE.
                rayColor = float4(0, 0, 0, 0);
                result.exited = true;
                return result;
            }
            result.terminalRadiance = hitAlbedo.rgb;
            result.exited = true;
            return result;
        }
        return result;
    }

    [mutating]
    public void Render<TCache : IRadianceCache>(IRayTracer tracer, IDirectIlluminator directIllum, inout TCache cache, int sampleMultiplier)
    {
        UniformBufferObject Camera = Bindless.GetGpuscene().Camera[0];

        if (!cache.ShouldShadePixel(pixel_, Camera))
        {
            return;
        }

        float4 FinalColor = float4(0, 0, 0, 0);
        float4 FinalReflection = float4(0, 0, 0, 0);
        float FinalDiffuseHitDist = 0;
        float FinalSpecularHitDist = 0;

        Material mat = Bindless.GetGpuscene().GetMaterial(hitPrimaryVertex_.MaterialIndex);

        const int sample = max(1, Camera.NumberOfSamples / SampleDownscale * sampleMultiplier);
        const bool wantsDirect = cache.WantsDirectLighting();

        for (int i = 0; i < sample; i++)
        {
            float4 RayColor = float4(1, 1, 1, 1);
            cache.BeginPath();

            float4 eye = mul(Camera.ModelViewInverse, float4(0, 0, 0, 1));
            float3 ray_dir = normalize(hitPrimaryVertex_.Position - eye.xyz);

            float3 direction = ray_dir;
            Vertex vertexStart = hitPrimaryVertex_;

            uint maxBounces = mat.MaterialModel == MaterialDielectric ? Camera.MaxNumberOfBounces : Camera.NumberOfBounces;

            // Accumulation flags / hit distance come from the first bounce only.
            bool hitReflect = false;
            bool hitMetal = false;
            float hitDist = 0;

            // Record the primary vertex as the path start.
            bool pathContinue = RecordSurfaceHit(directIllum, cache, wantsDirect, vertexStart, direction);

            for (uint b = 0; b < maxBounces && pathContinue; ++b)
            {
                const float3 rayColorBefore = RayColor.rgb;
                const bool suppressDiffuseEmitter = (b == 0) || wantsDirect;
                const bool suppressSpecularEmitter = (b == 0);
                FBounceResult bounce = ScatterAndTrace(tracer, vertexStart, direction, RayColor,
                                                       RandomSeed_, suppressDiffuseEmitter,
                                                       suppressSpecularEmitter);

                if (b == 0)
                {
                    hitReflect = bounce.hitReflect;
                    hitMetal = bounce.hitMetal;
                    hitDist = bounce.hitDist;
                }

                if (bounce.exited)
                {
                    if (bounce.hitMiss)
                    {
                        cache.OnMiss(bounce.terminalRadiance);
                    }
                    else if (any(bounce.terminalRadiance > float3(0, 0, 0)))
                    {
                        // Emissive terminator: record it as a hit with emissive radiance.
                        cache.OnSurfaceHit(vertexStart.Position, vertexStart.Normal, bounce.terminalRadiance, RandomSeed_);
                    }
                    break;
                }

                cache.OnSegmentThroughput(rayColorBefore, RayColor.rgb);
                pathContinue = RecordSurfaceHit(directIllum, cache, wantsDirect, vertexStart, direction);
                if (!pathContinue)
                {
                    break;
                }

                float4 cachedRadiance = float4(0, 0, 0, 0);
                if (cache.TryQueryRadiance(vertexStart, bounce.hitReflect, bounce.hitMetal, b + 1u, bounce.hitDist, cachedRadiance))
                {
                    RayColor *= cachedRadiance;
                    break;
                }

                if (b == 0)
                {
                    // First-bounce tail: optionally terminate immediately after the first hit.
                    if (ForceExitAfterFirst || (ExitAfterFirst && mat.MaterialModel != MaterialDielectric))
                    {
                        if (!FuzzyTracing)
                        {
                            ApplyAmbientTerminal(cache, vertexStart, RayColor);
                        }
                        break;
                    }
                    // Otherwise fall through to the next bounce (no secondary direct, no RR at bounce 0).
                }
                else
                {
                    // Secondary (post-first-bounce) direct lighting strategy.
                    if (!bounce.hitReflect)
                    {
                        if (SecondaryDirectMode == kSecondaryDirectTerminateIfLit)
                        {
                            FDirectLighting directLighting =
                                EvaluateDirectLighting(directIllum, vertexStart, direction);
                            float3 directRadiance = directLighting.DiffuseDemodulated;
                            if (any(directRadiance > float3(0, 0, 0)))
                            {
                                RayColor *= float4(directRadiance, 1);
                                break;
                            }
                        }
                        else if (SecondaryDirectMode == kSecondaryDirectPlainSun && Camera.HasSun && (RandomFloat(RandomSeed_) < 0.5f))
                        {
                            const float3 lightVector = Camera.SunDirection.xyz;
                            const float3 lightVectorCone = AlignWithNormal(RandomInCone(RandomSeed_, Common.SunCosAngularRadius()), lightVector);
                            float shadowTraceOffset = TRACE_CORRECTION_OFFSET;
                            if (EnableSoftTracingAdaptiveOffset)
                            {
                                shadowTraceOffset = max(TRACE_CORRECTION_OFFSET, GetAmbientCubeUnitAtPosition(vertexStart.Position) * 0.5f);
                            }
                            if (!tracer.TraceOcclusion(vertexStart.Position + vertexStart.Normal * shadowTraceOffset, lightVectorCone))
                            {
                                RayColor *= Camera.SunColor;
                                break;
                            }
                        }
                    }

                    // 提前退出
                    const bool earlyExit = (mat.MaterialModel != MaterialDielectric) && (RandomFloat(RandomSeed_) < ExitProbability);

                    // 终结路径
                    if (b == maxBounces - 1 || earlyExit)
                    {
                        if (mat.MaterialModel == MaterialDielectric)
                        {
                            RayColor *= float4(0, 0, 0, 1);
                        }
                        else
                        {
                            ApplyAmbientTerminal(cache, vertexStart, RayColor);
                        }
                        break;
                    }
                }
            }

            if (hitMetal)
            {
                RayColor *= primaryAlbedo_;
            }

            if (hitReflect)
            {
                FinalReflection += RayColor;
                FinalSpecularHitDist += hitDist;
            }
            else
            {
                FinalColor += RayColor;
                FinalDiffuseHitDist += hitDist;
            }

        }

        FinalColor /= float(sample);
        FinalReflection /= float(sample);
        FinalDiffuseHitDist /= float(sample);
        FinalSpecularHitDist /= float(sample);

        Material primaryMaterial = Bindless.GetGpuscene().GetMaterial(hitPrimaryVertex_.MaterialIndex);
        FBSDFContext primaryContext =
            BuildBSDFContext(hitPrimaryVertex_, primaryMaterial, primaryRayDir_);
        FDirectLighting directLighting = FDirectLighting();
        directIllum.DirectIlluminatePrimary(RandomSeed_, pixel_, primaryContext, directLighting);
        FinalColor.rgb += max(directLighting.DiffuseDemodulated, float3(0, 0, 0));
        FinalReflection.rgb += max(directLighting.SpecularRadiance, float3(0, 0, 0));
        FinalReflection.rgb += max(EvaluateGlossyDirect(tracer, primaryContext,
                                                        hitPrimaryVertex_, HitNormalOffset,
                                                        RandomSeed_), float3(0, 0, 0));

        if (WriteOutputs)
        {
            OutSingleDiffuse.Store(pixel_, FinalColor);
            OutSingleSpecular.Store(pixel_, FinalReflection);
            OutDiffuseHitDist.Store(pixel_, FinalDiffuseHitDist + primaryRayLength_);
            OutSpecularHitDist.Store(pixel_, FinalSpecularHitDist + primaryRayLength_);
        }
    }

    [mutating]
    public bool PrimaryHit(IPrimaryRayCaster primaryRayCaster)
    {
        let Camera = Bindless.GetGpuscene().Camera[0];

        if (!primaryRayCaster.TracePrimaryRay(pixel_, size_, RandomSeed_, hitPrimaryVertex_, hitPrimaryNode_, primaryRayDir_, primaryRayLength_, primaryPixelOffset_))
        {
            float4 skyColor = Common.SampleSkyRadiance(primaryRayDir_, 0.0f);
            skyColor.rgb += Common.EvaluateAnalyticSunDisk(primaryRayDir_);
            float2 skyMotionPixels =
                Common.CalculateSkyMotionVector(Camera, primaryRayDir_) * size_;

            if (WriteOutputs)
            {
                OutSingleDiffuse.Store(pixel_, skyColor);
                OutMotionVector.Store(pixel_, float4(skyMotionPixels, 0, 0));
                OutAlbedo.Store(pixel_, float4(1, 1, 1, 1));
                OutNormal.Store(pixel_, float4(0, 1, 0, 1));
                OutObjectID.Store(pixel_, 0);
                OutDiffuseHitDist.Store(pixel_, 1000);
                OutSpecularHitDist.Store(pixel_, 1000);
                OutSpecularAlbedo.Store(pixel_, float4(0, 0, 0, 0));
                OutBSDFData.Store(pixel_, uint2(0xFFFFFFFFu, 0u));
            }

            return false;
        }

        float4 clipPos = mul(Camera.ViewProjectionUnJit, float4(hitPrimaryVertex_.Position, 1.0));
        float ndcDepth = clipPos.z / clipPos.w;
        float2 motion = Common.CalculateMotionVector(Camera, hitPrimaryNode_, hitPrimaryVertex_, pixel_);
        primaryMotionPixels_ = motion * size_;
        float4 gbuffer = float4(0, 0, 0, 0);
        Material material = Bindless.GetGpuscene().GetMaterial(hitPrimaryVertex_.MaterialIndex);
        Common.FetchGBuffer(hitPrimaryVertex_, material, hitPrimaryNode_, primaryRayDir_, primaryAlbedo_, gbuffer);
        FBSDFContext outputContext =
            BuildBSDFContext(hitPrimaryVertex_, material, primaryRayDir_);

        if (WriteOutputs)
        {
            OutAlbedo.Store(pixel_, primaryAlbedo_);
            OutNormal.Store(pixel_, gbuffer);
            const bool selected = hitPrimaryNode_.reserved1 != 0;
            const bool hovered = (hitPrimaryNode_.reserved2 & 1u) != 0;
            const bool locked = (hitPrimaryNode_.reserved2 & 2u) != 0;
            const bool danger = (hitPrimaryNode_.reserved2 & 4u) != 0;
            OutObjectID.Store(pixel_, Common.EncodeObjectId(hitPrimaryNode_.instanceId, selected, hovered, locked, danger));
            // The engine-wide upscaler contract stores motion in render-pixel
            // units. Streamline and FidelityFX can therefore both use a 1:1
            // motion-vector scale.
            OutMotionVector.Store(pixel_, float4(primaryMotionPixels_, 0, 0));
            OutPrevDepth.Store(pixel_, ndcDepth);
            OutBSDFData.Store(pixel_, uint2(hitPrimaryVertex_.MaterialIndex,
                                           asuint(outputContext.Metalness)));
        }

        if (material.MaterialModel == MaterialDiffuseLight)
        {
            if (WriteOutputs)
            {
                OutAlbedo.Store(pixel_, float4(1, 1, 1, 0));
                OutSingleDiffuse.Store(pixel_, primaryAlbedo_);
                OutSingleSpecular.Store(pixel_, float4(0, 0, 0, 0));
                OutDiffuseHitDist.Store(pixel_, 10);
                OutSpecularHitDist.Store(pixel_, 10);
                OutSpecularAlbedo.Store(pixel_, float4(0, 0, 0, 0));
                OutBSDFData.Store(pixel_, uint2(0xFFFFFFFFu, 0u));
            }
            return false;
        }

        float4 specAlbedo = material.MaterialModel == MaterialMetallic ? primaryAlbedo_ : float4(1, 1, 1, 1);
        if (WriteOutputs)
        {
            OutSpecularAlbedo.Store(pixel_, specAlbedo);
        }

        if (material.MaterialModel != MaterialDielectric)
        {
            hitPrimaryVertex_.Position = hitPrimaryVertex_.Position - primaryRayDir_ * Camera.SceneEpsilonScale * 0.01f;// - rayDir * rayLength * 0.005;
        }

        return true;
    }

    public uint2 GetSize() { return size_; }
    public Vertex GetPrimaryVertex() { return hitPrimaryVertex_; }
    // Primary-hit data for consumers inside the same dispatch (ReSTIR temporal gates).
    // Reading the freshly written G-buffer back is NOT coherent within an invocation, so
    // this state must come from the renderer, never from the output textures.
    public uint GetPrimaryInstanceId() { return hitPrimaryNode_.instanceId; }
    public float2 GetPrimaryMotionPixels() { return primaryMotionPixels_; }

    // Debug write that only replaces the lighting color and leaves the real G-buffer
    // (object id, motion, normals) intact. Debug overlays that run every frame must use
    // this, or they destroy the very temporal state they are trying to visualize.
    public void WriteDebugColorOnly(float4 color)
    {
        OutSingleDiffuse.Store(pixel_, color);
        OutSingleSpecular.Store(pixel_, float4(0, 0, 0, 0));
    }

    // Generic debug-color G-buffer write (used by entry-shader debug views).
    public void WriteDebugColor(float4 color)
    {
        OutSingleDiffuse.Store(pixel_, color);
        OutSingleSpecular.Store(pixel_, float4(0, 0, 0, 0));
        OutAlbedo.Store(pixel_, float4(1, 1, 1, 1));
        OutNormal.Store(pixel_, float4(0, 1, 0, 1));
        OutObjectID.Store(pixel_, 0);
        OutMotionVector.Store(pixel_, float4(0, 0, 0, 0));
        OutPrevDepth.Store(pixel_, 1.0f);
        OutDiffuseHitDist.Store(pixel_, 1000.0f);
        OutSpecularHitDist.Store(pixel_, 1000.0f);
        OutSpecularAlbedo.Store(pixel_, float4(0, 0, 0, 0));
        OutBSDFData.Store(pixel_, uint2(0xFFFFFFFFu, 0u));
    }

    // Debug view: cached radiance modulated by primary albedo. The cache query
    // is performed by the entry shader and its result passed in here so the
    // renderer stays free of any cache-specific symbols.
    public void WriteDebugCachedRadiance(float4 cachedRadiance, bool hit)
    {
        float4 outColor = float4(0, 0, 0, 1);
        if (hit)
        {
            outColor = float4(max(cachedRadiance.rgb * primaryAlbedo_.rgb, float3(0, 0, 0)), 1);
        }
        WriteDebugColor(outColor);
    }

    [mutating]
    public void Init(uint2 ipos)
    {
        let Camera = Bindless.GetGpuscene().Camera[0];
        pixel_ = ipos;
        RandomSeed_ = InitRandomSeed(ipos.x, ipos.y, Camera.TotalFrames);
    }

    public __init()
    {
        pixel_ = int2(0, 0);
        size_ = Bindless.GetViewStorageTextureDimensions<float4>(Bindless.RT_SINGLE_DIFFUSE);

        RandomSeed_ = uint4(0, 0, 0, 0);
        hitPrimaryVertex_ = {};
        hitPrimaryNode_ = {};
        primaryRayDir_ = float3(0, 1, 0);
        primaryRayLength_ = 0;
        primaryPixelOffset_ = float2(0, 0);
        primaryMotionPixels_ = float2(0, 0);
        primaryAlbedo_ = float4(0, 0, 0, 0);

        OutAlbedo = Bindless.GetViewStorageTexture<float4>(Bindless.RT_ALBEDO);
        OutNormal = Bindless.GetViewStorageTexture<float4>(Bindless.RT_NORMAL);
        OutObjectID = Bindless.GetViewStorageTexture<uint>(Bindless.RT_OBJECTID_0);
        OutMotionVector = Bindless.GetViewStorageTexture<float4>(Bindless.RT_MOTIONVECTOR);
        OutPrevDepth = Bindless.GetViewStorageTexture<float>(Bindless.RT_PREV_DEPTHBUFFER);
        OutSingleDiffuse = Bindless.GetViewStorageTexture<float4>(Bindless.RT_SINGLE_DIFFUSE);
        OutSingleSpecular = Bindless.GetViewStorageTexture<float4>(Bindless.RT_SINGLE_SPECULAR);
        OutDiffuseHitDist = Bindless.GetViewStorageTexture<float>(Bindless.RT_DIFFUSE_HITDIST);
        OutSpecularHitDist = Bindless.GetViewStorageTexture<float>(Bindless.RT_SPECULAR_HITDIST);
        OutSpecularAlbedo = Bindless.GetViewStorageTexture<float4>(Bindless.RT_SPECULAR_ALBEDO);
        OutBSDFData = Bindless.GetViewStorageTexture<uint2>(Bindless.RT_BSDF_DATA);
    }

    int2 pixel_;
    uint2 size_;
    uint4 RandomSeed_;

    Vertex hitPrimaryVertex_;
    NodeProxy hitPrimaryNode_;
    float3 primaryRayDir_;
    float primaryRayLength_;
    float2 primaryPixelOffset_;
    float2 primaryMotionPixels_;
    float4 primaryAlbedo_;

    RWTexture2D<float4> OutAlbedo;
    RWTexture2D<float4> OutNormal;
    RWTexture2D<uint> OutObjectID;
    RWTexture2D<float4> OutMotionVector;
    RWTexture2D<float> OutPrevDepth;
    RWTexture2D<float4> OutSingleDiffuse;
    RWTexture2D<float4> OutSingleSpecular;
    RWTexture2D<float> OutDiffuseHitDist;
    RWTexture2D<float> OutSpecularHitDist;
    RWTexture2D<float4> OutSpecularAlbedo;
    RWTexture2D<uint2> OutBSDFData;
}
