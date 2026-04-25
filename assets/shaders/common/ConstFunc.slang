implementing Common;
#include "PreProcessor.slang"

float pow5(float x) { return x*x*x*x*x; }
float pow4(float x) { return x*x*x*x; }

// Polynomial approximation by Christophe Schlick
public float Schlick(const float cosine, const float refractionIndex)
{
    float r0 = (1 - refractionIndex) / (1 + refractionIndex);
    r0 *= r0;
    // taken from https://www.photometric.io/blog/improving-schlicks-approximation/
    return r0 + (1 - r0) * pow5(1 - cosine);
}

//onb revisited by Disney
void ONB(float3 n, out float3 b1, out float3 b2) {
    float signZ = n.z < 0.f ? -1.f : 1.f;
    float a = -1.0f / (signZ + n.z);
    b2 = float3(n.x * n.y * a, signZ + n.y * n.y * a, -n.y);
    b1 = float3(1.0f + signZ * n.x * n.x * a, signZ * b2.x, -signZ * n.x);
}

void ONBAlignWithNormal(float3 up, out float3 right, out float3 forward)
{
    right = normalize(cross(up, float3(0.0072f, 1.0f, 0.0034f)));
    forward = cross(right, up);
}

void orthonormalBasis(float3 normal, out float3 tangent, out float3 bitangent)
{
    if(normal.z < -0.99998796F)  // Handle the singularity
    {
        tangent   = float3(0.0F, -1.0F, 0.0F);
        bitangent = float3(-1.0F, 0.0F, 0.0F);
        return;
    }
    float a   = 1.0F / (1.0F + normal.z);
    float b   = -normal.x * normal.y * a;
    tangent   = float3(1.0F - normal.x * normal.x * a, b, -normal.x);
    bitangent = float3(b, 1.0f - normal.y * normal.y * a, -normal.y);
}

public float3 AlignWithNormal(float3 ray, float3 normal)
{
    float3 T, B;
    ONB(normal, T, B);
    return to_world(ray, T, B, normal);
}


// hsv to rgb
float3 hsv2rgb(float3 c) {
    float4 K = float4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    float3 p = abs(frac(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * lerp(K.xxx, saturate(p - K.xxx), c.y);
}

float3 rgb2yuv(float3 rgb) {
    float Y = 0.299 * rgb.r + 0.587 * rgb.g + 0.114 * rgb.b;
    float U = -0.14713 * rgb.r - 0.28886 * rgb.g + 0.436 * rgb.b;
    float V = 0.615 * rgb.r - 0.51499 * rgb.g - 0.10001 * rgb.b;
    return float3(Y, U, V);
}

float3 yuv2rgb(float3 yuv) {
    float Y = yuv.x;
    float U = yuv.y;
    float V = yuv.z;
    float R = Y + 1.13983 * V;
    float G = Y - 0.39465 * U - 0.58060 * V;
    float B = Y + 2.03211 * U;
    return float3(R, G, B);
}

public float3 rgb2ycocg(float3 rgb) {
    float Y  = 0.25 * rgb.r + 0.5 * rgb.g + 0.25 * rgb.b;
    float Co = 0.5 * rgb.r - 0.5 * rgb.b;
    float Cg = -0.25 * rgb.r + 0.5 * rgb.g - 0.25 * rgb.b;
    return float3(Y, Co, Cg);
}

public float3 ycocg2rgb(float3 ycocg) {
    float Y  = ycocg.r;
    float Co = ycocg.g;
    float Cg = ycocg.b;
    float R = Y + Co - Cg;
    float G = Y + Cg;
    float B = Y - Co - Cg;
    return float3(R, G, B);
}

float hash(float n) {
    return frac(sin(n) * 43758.5453123);
}

float3 uintToColor(uint id) {
    // 将 id 转换为 float 并应用散列
    float h = hash(float(id) * 0.0001);
    float hue = frac(h * 0.95); // 取 [0, 1) 范围内的色相
    float saturation = 0.6 + hash(float(id + 1)) * 0.3; // 保持较高的饱和度 [0.6, 0.9)
    float value = 0.8 + hash(float(id + 2)) * 0.2; // 保持较高的亮度 [0.8, 1.0)

    return hsv2rgb(float3(hue, saturation, value));
}

// sRGB to linear approximation, see http://chilliant.blogspot.com/2012/08/srgb-approximations-for-hlsl.html
float4 srgbToLinear(in float4 sRgb)
{
    //return float4(pow(sRgb.xyz, float3(1.0f / 2.2f, 1.0f / 2.2f, 1.0f / 2.2f)), sRgb.w);
    //float3 rgb = sRgb.xyz * (sRgb.xyz * (sRgb.xyz * 0.305306011F + 0.682171111F) + 0.012522878F);
    return float4(sRgb.rgb, sRgb.a);
}

// ---------- Packing helpers (shared by AmbientCube / DistanceField shaders) ----------

#define MAX_ILLUMINANCE 512.f

// Software replacements for hw-dependent pack_u8 / unpack_u8u32.
public uint4 UnpackBytes(uint packed)
{
    return uint4(
        (packed & 0xFF),
        ((packed >> 8) & 0xFF),
        ((packed >> 16) & 0xFF),
        ((packed >> 24) & 0xFF)
    );
}

public uint PackBytes(uint4 values)
{
    return (values.x & 0xFF) |
           ((values.y & 0xFF) << 8) |
           ((values.z & 0xFF) << 16) |
           ((values.w & 0xFF) << 24);
}

// RGB10A2 color pack/unpack using MAX_ILLUMINANCE as the HDR clamp range.
public uint packRGB10A2(float4 color)
{
    float4 clamped = clamp(color / MAX_ILLUMINANCE, float4(0.0f), float4(1.0f));

    uint r = uint(clamped.r * 1023.0f);
    uint g = uint(clamped.g * 1023.0f);
    uint b = uint(clamped.b * 1023.0f);
    uint a = uint(clamped.a * 3.0f);

    return r | (g << 10) | (b << 20) | (a << 30);
}

public float4 unpackRGB10A2(uint packed)
{
    float r = float((packed) & 0x3FF) / 1023.0f;
    float g = float((packed >> 10) & 0x3FF) / 1023.0f;
    float b = float((packed >> 20) & 0x3FF) / 1023.0f;

    return float4(r, g, b, 0.0) * MAX_ILLUMINANCE;
}

void pcg4d(inout uint4 v)
{
    v = v * 1664525u + 1013904223u;
    v.x += v.y * v.w; v.y += v.z * v.x; v.z += v.x * v.y; v.w += v.y * v.z;
    v = v ^ (v >> 16u);
    v.x += v.y * v.w; v.y += v.z * v.x; v.z += v.x * v.y; v.w += v.y * v.z;
}
// Returns a float between 0 and 1
#define uint_to_float(x) ( asfloat(0x3f800000 | ((x) >> 9)) - 1.0f )

public uint4 InitRandomSeed(uint val0, uint val1, uint frame_num)
{
    return uint4(val0, val1, frame_num, 0);
}

public float RandomFloat(inout uint4 v)
{
    pcg4d(v);
    return uint_to_float(v.x);
}

public float2 RandomFloat2(inout uint4 v)
{
    pcg4d(v);
    return uint_to_float(v.xy);
}

public float3 RandomFloat3(inout uint4 v)
{
    pcg4d(v);
    return uint_to_float(v.xyz);
}

float2 concentric_sample_disk(float2 offset) {
    offset += offset - float2(1);
    if (isZERO(offset.x) && isZERO(offset.y)) {
        return float2(0);
    }

    float theta;

    if (abs(offset.x) > abs(offset.y)) {
        theta = M_PI_4 * offset.y / offset.x;
        return offset.x * float2(cos(theta), sin(theta));
    }

    float cos_theta = sin(M_PI_4 * offset.x / offset.y);
    return offset.y * float2(cos_theta, sqrt(1. - cos_theta * cos_theta));
}

public float2 RandomInUnitDisk(inout uint4 seed)
{
    return concentric_sample_disk(RandomFloat2(seed));
}

public float3 RandomInCone(inout uint4 seed, float cos_theta) {
    const float2 u = RandomFloat2(seed);
    float phi = M_TWO_PI * u.x;

    cos_theta = 1.0f + u.y * (cos_theta - 1.f);

    return float3(sqrt(1.0f - cos_theta * cos_theta) * float2(cos(phi), sin(phi)), cos_theta);
}

public float3 RandomInHemiSphere(inout uint4 seed)
{
    const float2 u = RandomFloat2(seed);
    float phi = M_TWO_PI * u.x;

    return float3(sqrt(1.0f - u.y) * float2(cos(phi), sin(phi)), sqrt(u.y));
}

public float3 RandomInHemiSphere1(inout uint4 seed)
{
    const float2 u = RandomFloat2(seed);
    float phi = M_TWO_PI * u.x;

    return float3(sqrt(u.y) * float2(cos(phi), sin(phi)), sqrt(1.0-u.y));
}

// Kenta Eto and Yusuke Tokuyoshi. 2023. Bounded VNDF Sampling for Smith-GGX Reflections.
// In SIGGRAPH Asia 2023 Technical Communications (SA Technical Communications '23), December 12-15, 2023, Sydney, NSW, Australia. ACM, New York, NY, USA, 4 pages.
// https://doi.org/10.1145/3610543.3626163
float3 ggx_sample_vndf(float2 alpha, float3 wi_, float2 uv) {
    float3 wi = normalize(float3(wi_.xy * alpha, wi_.z));
    // Sample a spherical cap
    //float k = (1.0f - a2) * s2 / (s2 + a2 * wi_.z * wi_.z); // Eq. 5

    float b = wi.z;
    if(wi_.z > 0.f) {
        float a = saturate(min(alpha.x, alpha.y)); // Eq. 6
        float awiz_s = a * wi_.z / (1.0f + length(wi_.xy));
        b *= ((1.0f - a * a) / (1.0f + awiz_s * awiz_s));
    }

    float z = fma(1.0f - uv.y, 1.0f + b, -b);
    float phi = M_TWO_PI * uv.x;
    float3 o_std = float3(sqrt(saturate(1.0f - z * z)) * float2(cos(phi), sin(phi)), z);
    // Compute the microfacet normal m
    float3 m_std = wi + o_std;
    return normalize(float3(m_std.xy * alpha, m_std.z));
}

public float3 ggxSampling(inout uint4 RandomSeed, float roughness, float3 normal)
{
    float3 tangent, bitangent;
    ONB(normal, tangent, bitangent);
    float3 wm_ = ggx_sample_vndf(
        float2(roughness * roughness),
        to_local(normal, tangent, bitangent, normal),
        RandomFloat2(RandomSeed)
    );
    return to_world(wm_, tangent, bitangent, normal);
}

public Vertex UnpackVertex(in GPUVertex Vert)
{
    Vertex v;
    v.Position = Vert.Position_Tx.xyz;
    v.Normal = Vert.Normal_Ty.xyz;
    v.Tangent = Vert.Tangent;
    v.TexCoord = float2(Vert.Position_Tx.w, Vert.Normal_Ty.w);
    uint16_t TangentWMatIdx = asuint16(Vert.Tangent.w);
    v.Tangent.w = (TangentWMatIdx >> 8) & 0xFF;
    v.MaterialIndex = TangentWMatIdx & 0xFF;

    return v;
}

public uint FetchMaterialId(in NodeProxy node, uint offset)
{
    uint materialIndex = 0;
    switch (offset) {
    case 0:
        materialIndex = node.matId[0];
        break;
    case 1:
        materialIndex = node.matId[1];
        break;
    case 2:
        materialIndex = node.matId[2];
        break;
    case 3:
        materialIndex = node.matId[3];
        break;
    case 4:
        materialIndex = node.matId[4];
        break;
    case 5:
        materialIndex = node.matId[5];
        break;
    case 6:
        materialIndex = node.matId[6];
        break;
    case 7:
        materialIndex = node.matId[7];
        break;
    case 8:
        materialIndex = node.matId[8];
        break;
    case 9:
        materialIndex = node.matId[9];
        break;
    case 10:
        materialIndex = node.matId[10];
        break;
    case 11:
        materialIndex = node.matId[11];
        break;
    case 12:
        materialIndex = node.matId[12];
        break;
    case 13:
        materialIndex = node.matId[13];
        break;
    case 14:
        materialIndex = node.matId[14];
        break;
    case 15:
        materialIndex = node.matId[15];
        break;
    }
    return materialIndex;
}

public float3 heatmap(float t)
{
    const float3 c[10] = {
        float3(0.0f / 255.0f, 2.0f / 255.0f, 91.0f / 255.0f),
        float3(0.0f / 255.0f, 108.0f / 255.0f, 251.0f / 255.0f),
        float3(0.0f / 255.0f, 221.0f / 255.0f, 221.0f / 255.0f),
        float3(51.0f / 255.0f, 221.0f / 255.0f, 0.0f / 255.0f),
        float3(255.0f / 255.0f, 252.0f / 255.0f, 0.0f / 255.0f),
        float3(255.0f / 255.0f, 180.0f / 255.0f, 0.0f / 255.0f),
        float3(255.0f / 255.0f, 104.0f / 255.0f, 0.0f / 255.0f),
        float3(226.0f / 255.0f, 22.0f / 255.0f, 0.0f / 255.0f),
        float3(191.0f / 255.0f, 0.0f / 255.0f, 83.0f / 255.0f),
        float3(145.0f / 255.0f, 0.0f / 255.0f, 65.0f / 255.0f)
    };

    const float s = t * 10.0f;

    const int cur = int(s) <= 9 ? int(s) : 9;
    const int prv = cur >= 1 ? cur - 1 : 0;
    const int nxt = cur < 9 ? cur + 1 : 9;

    const float blur = 0.8f;

    const float wc = smoothstep(float(cur) - blur, float(cur) + blur, s) * (1.0f - smoothstep(float(cur + 1) - blur, float(cur + 1) + blur, s));
    const float wp = 1.0f - smoothstep(float(cur) - blur, float(cur) + blur, s);
    const float wn = smoothstep(float(cur + 1) - blur, float(cur + 1) + blur, s);

    const float3 r = wc * c[cur] + wp * c[prv] + wn * c[nxt];
    return float3(clamp(r.x, 0.0f, 1.0f), clamp(r.y, 0.0f, 1.0f), clamp(r.z, 0.0f, 1.0f));
}