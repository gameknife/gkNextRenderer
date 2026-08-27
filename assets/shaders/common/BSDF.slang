implementing PathTracing;

#ifndef GK_NEXT_BSDF_SLANG
#define GK_NEXT_BSDF_SLANG

#include "PreProcessor.slang"

// Shared material evaluation for tracing direct lighting. PerceptualRoughness follows
// glTF; GGX alpha is roughness^2 everywhere in this file.
public static const float BSDF_MIN_ROUGHNESS = 0.001f;
public static const float BSDF_DELTA_ROUGHNESS = 0.02f;

public static const uint BSDF_LOBE_NONE = 0u;
public static const uint BSDF_LOBE_DIFFUSE = 1u << 0;
public static const uint BSDF_LOBE_REFLECTION = 1u << 1;
public static const uint BSDF_LOBE_TRANSMISSION = 1u << 2;
public static const uint BSDF_LOBE_DELTA = 1u << 3;

public struct FBSDFContext
{
    public float3 Position;
    public float3 Normal;
    public float3 Wo;
    public float3 BaseColor;
    public float PerceptualRoughness;
    public float Metalness;
    public float EtaOutside;
    public float EtaInside;
    public uint MaterialModel;
    public bool FrontFace;
};

public struct FBSDFEval
{
    // Diffuse excludes BaseColor because RT_SINGLE_DIFFUSE is albedo-demodulated.
    public float3 DiffuseDemodulated;
    // Specular contains the complete BRDF/BTDF value.
    public float3 Specular;
    public float DiffusePdf;
    public float SpecularPdf;
    public uint Flags;

    public __init()
    {
        DiffuseDemodulated = float3(0, 0, 0);
        Specular = float3(0, 0, 0);
        DiffusePdf = 0.0f;
        SpecularPdf = 0.0f;
        Flags = BSDF_LOBE_NONE;
    }
};

public struct FDirectLighting
{
    public float3 DiffuseDemodulated;
    public float3 SpecularRadiance;

    public __init()
    {
        DiffuseDemodulated = float3(0, 0, 0);
        SpecularRadiance = float3(0, 0, 0);
    }
};

public float3 BSDFDielectricF0(float eta)
{
    const float e = max(eta, 1.0001f);
    const float f = (e - 1.0f) / (e + 1.0f);
    return float3(f * f);
}

public float3 BSDFFresnelSchlick(float cosTheta, float3 f0)
{
    const float m = 1.0f - saturate(cosTheta);
    const float m2 = m * m;
    return f0 + (1.0f - f0) * (m2 * m2 * m);
}

public float BSDFDistributionGGX(float NoH, float perceptualRoughness)
{
    const float alpha = max(perceptualRoughness * perceptualRoughness,
                            BSDF_MIN_ROUGHNESS * BSDF_MIN_ROUGHNESS);
    const float alpha2 = alpha * alpha;
    const float d = NoH * NoH * (alpha2 - 1.0f) + 1.0f;
    return alpha2 / max(M_PI * d * d, 1e-7f);
}

public float BSDFGeometrySmithG1(float NoX, float perceptualRoughness)
{
    if (NoX <= 0.0f)
    {
        return 0.0f;
    }
    const float alpha = max(perceptualRoughness * perceptualRoughness,
                            BSDF_MIN_ROUGHNESS * BSDF_MIN_ROUGHNESS);
    const float alpha2 = alpha * alpha;
    return (2.0f * NoX) /
           max(NoX + sqrt(alpha2 + (1.0f - alpha2) * NoX * NoX), 1e-7f);
}

public float BSDFGeometrySmith(float NoV, float NoL, float perceptualRoughness)
{
    return BSDFGeometrySmithG1(NoV, perceptualRoughness) *
           BSDFGeometrySmithG1(NoL, perceptualRoughness);
}

public FBSDFContext BuildBSDFContext(Vertex vertex, Material material, float3 incomingRayDirection)
{
    FBSDFContext context;
    context.Position = vertex.Position;
    context.FrontFace = dot(incomingRayDirection, vertex.Normal) <= 0.0f;
    context.Normal = normalize(context.FrontFace ? vertex.Normal : -vertex.Normal);
    context.Wo = normalize(-incomingRayDirection);
    context.BaseColor = material.Diffuse.rgb;
    context.PerceptualRoughness = material.Fuzziness;
    context.Metalness = saturate(material.Metalness);
    context.EtaOutside = 1.0f;
    // Mixture is an opaque dielectric/metal blend and stores its surface IOR in
    // RefractionIndex. RefractionIndex2 is the transmitted medium used by Dielectric.
    const float surfaceIor = material.MaterialModel == MaterialDielectric
        ? material.RefractionIndex2
        : material.RefractionIndex;
    context.EtaInside = max(surfaceIor, 1.0001f);
    context.MaterialModel = material.MaterialModel;

    if (material.DiffuseTextureId >= 0)
    {
        context.BaseColor *= Bindless.GetSampleTexture(material.DiffuseTextureId)
                                     .SampleLevel(vertex.TexCoord, 0).rgb;
    }
    if (material.MRATextureId >= 0)
    {
        const float4 mra = Bindless.GetSampleTexture(material.MRATextureId)
                                   .SampleLevel(vertex.TexCoord, 0);
        context.PerceptualRoughness *= mra.g;
        context.Metalness *= mra.b;
    }
    if (material.MaterialModel == MaterialMetallic)
    {
        context.Metalness = 1.0f;
    }
    else if (material.MaterialModel == MaterialDielectric)
    {
        context.Metalness = 0.0f;
    }

    context.PerceptualRoughness = saturate(context.PerceptualRoughness);
    return context;
}

// Build a BSDF context from an already-resolved Primary Surface. Identical to BuildBSDFContext
// except that base color, roughness and metalness are supplied rather than sampled: a surface
// reconstructed from depth has no TexCoord, and those three values are precisely what the texture
// samples in BuildBSDFContext produce. The material is still needed for the IOR and the model.
public FBSDFContext BuildBSDFContextFromSurface(
    float3 position, float3 normal, float3 incomingRayDirection, Material material,
    float3 baseColor, float perceptualRoughness, float metalness)
{
    FBSDFContext context;
    context.Position = position;
    context.FrontFace = dot(incomingRayDirection, normal) <= 0.0f;
    context.Normal = normalize(context.FrontFace ? normal : -normal);
    context.Wo = normalize(-incomingRayDirection);
    context.BaseColor = baseColor;
    context.PerceptualRoughness = saturate(perceptualRoughness);
    context.Metalness = saturate(metalness);
    context.EtaOutside = 1.0f;
    const float surfaceIor = material.MaterialModel == MaterialDielectric
        ? material.RefractionIndex2
        : material.RefractionIndex;
    context.EtaInside = max(surfaceIor, 1.0001f);
    context.MaterialModel = material.MaterialModel;
    return context;
}

public float3 BSDFMaterialF0(FBSDFContext context)
{
    const float3 dielectricF0 = BSDFDielectricF0(context.EtaInside / context.EtaOutside);
    return lerp(dielectricF0, context.BaseColor, context.Metalness);
}

public FBSDFEval EvaluateSurfaceBSDF(FBSDFContext context, float3 wi)
{
    FBSDFEval result = FBSDFEval();
    const float3 N = context.Normal;
    const float3 V = normalize(context.Wo);
    const float3 L = normalize(wi);
    const float NoV = max(dot(N, V), 0.0f);
    const float NoL = dot(N, L);
    if (NoV <= 0.0f || NoL <= 0.0f)
    {
        return result;
    }

    if (context.MaterialModel == MaterialLambertian)
    {
        result.DiffuseDemodulated = float3(M_1_PI);
        result.DiffusePdf = NoL * M_1_PI;
        result.Flags = BSDF_LOBE_DIFFUSE;
        return result;
    }
    if (context.MaterialModel == MaterialDiffuseLight ||
        context.MaterialModel == MaterialIsotropic)
    {
        return result;
    }

    const float3 H = normalize(V + L);
    const float NoH = max(dot(N, H), 0.0f);
    const float VoH = max(dot(V, H), 0.0f);
    const float3 F = BSDFFresnelSchlick(VoH, BSDFMaterialF0(context));
    const float D = BSDFDistributionGGX(NoH, context.PerceptualRoughness);
    const float G = BSDFGeometrySmith(NoV, NoL, context.PerceptualRoughness);

    result.Specular = (D * G * F) / max(4.0f * NoV * NoL, 1e-7f);
    result.SpecularPdf = D * NoH / max(4.0f * VoH, 1e-7f);
    result.Flags |= BSDF_LOBE_REFLECTION;
    if (context.PerceptualRoughness <= BSDF_DELTA_ROUGHNESS)
    {
        result.Flags |= BSDF_LOBE_DELTA;
    }

    if (context.MaterialModel == MaterialMixture)
    {
        const float3 kD = (1.0f - F) * (1.0f - context.Metalness);
        result.DiffuseDemodulated = kD * M_1_PI;
        result.DiffusePdf = NoL * M_1_PI;
        result.Flags |= BSDF_LOBE_DIFFUSE;
    }
    return result;
}

public float PowerHeuristic(float pdfA, float pdfB)
{
    const float a2 = pdfA * pdfA;
    const float b2 = pdfB * pdfB;
    return a2 / max(a2 + b2, 1e-12f);
}

// A normalized Phong-like proposal around the ideal reflection direction. The evaluated
// BSDF remains GGX; this proposal is used only for direct-light MIS and has an exact PDF.
public float BSDFGlossyProposalExponent(float perceptualRoughness)
{
    const float r2 = max(perceptualRoughness * perceptualRoughness, 1e-5f);
    return min(max(2.0f / r2 - 2.0f, 0.0f), 10000.0f);
}

public float EvaluateGlossyProposalPdf(FBSDFContext context, float3 wi)
{
    if (context.PerceptualRoughness <= BSDF_DELTA_ROUGHNESS)
    {
        return 0.0f;
    }
    const float3 reflected = normalize(reflect(-context.Wo, context.Normal));
    const float cosTheta = max(dot(reflected, normalize(wi)), 0.0f);
    const float exponent = BSDFGlossyProposalExponent(context.PerceptualRoughness);
    return (exponent + 1.0f) * M_1_OVER_TWO_PI * pow(cosTheta, exponent);
}

public float3 SampleGlossyProposal(inout uint4 randomSeed, FBSDFContext context, out float pdf)
{
    const float exponent = BSDFGlossyProposalExponent(context.PerceptualRoughness);
    const float2 u = RandomFloat2(randomSeed);
    const float cosTheta = pow(max(u.x, 1e-7f), 1.0f / (exponent + 1.0f));
    const float sinTheta = sqrt(max(1.0f - cosTheta * cosTheta, 0.0f));
    const float phi = M_TWO_PI * u.y;
    const float3 localDirection =
        float3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
    const float3 reflected = normalize(reflect(-context.Wo, context.Normal));
    const float3 wi = normalize(AlignWithNormal(localDirection, reflected));
    pdf = (exponent + 1.0f) * M_1_OVER_TWO_PI * pow(cosTheta, exponent);
    return wi;
}

#endif
