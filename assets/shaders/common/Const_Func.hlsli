#ifndef CONST_FUNC_H

#define to_world(v, t, b, n) (mul(float3x3((t), (b), (n)), (v)))
#define to_local(v, T, B, N) (mul((v), float3x3((T), (B), (N))))

#define M_PI            3.14159265358979323846  // pi
#define M_TWO_PI        6.283185307179586476925 // 2*pi
#define M_PI_2          1.57079632679489661923  // pi/2
#define M_PI_4          0.785398163397448309616 // pi/4
#define M_1_PI          0.318309886183790671538 // 1/pi
#define M_2_PI          0.636619772367581343076 // 2/pi
#define M_1_OVER_TWO_PI 0.159154943091895335768 // 1/(2*pi)

#define INF 1e32f
#define EPS 1e-3f
#define EPS2 2e-3f

#define NEARzero 1e-35f
#define isZERO(x) ((x) > -NEARzero && (x) < NEARzero)
#define allLessThanZERO(a)  ((a).x + (a).y + (a).z < NEARzero)
#define allGreaterZERO(a)   ((a).x + (a).y + (a).z >= NEARzero)
#define sum_is_empty_abs(a) (abs((a).x) + abs((a).y) + abs((a).z) < NEARzero)
#define sum_is_not_empty_abs(a) (abs((a).x) + abs((a).y) + abs((a).z) >= NEARzero)

#define Mix(a, b, c, bary) ((a) + ((b) - (a)) * (bary).y + ((c) - (a)) * (bary).z)
#define luminance(rgb) (dot((rgb), float3(0.212671F, 0.715160F, 0.072169F)))

#define saturate(x) (clamp((x), 0.0F, 1.0F))

float pow5(float x) { return x*x*x*x*x; }
float pow4(float x) { return x*x*x*x; }

// Polynomial approximation by Christophe Schlick
float Schlick(const float cosine, const float refractionIndex)
{
    float r0 = (1 - refractionIndex) / (1 + refractionIndex);
    r0 *= r0;
    // taken from https://www.photometric.io/blog/improving-schlicks-approximation/
    return r0 + (1 - cosine - r0) * pow4(1 - cosine);
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

float3 AlignWithNormal(float3 ray, float3 normal)
{
    float3 T, B;
    ONB(normal, T, B);
    return to_world(ray, T, B, normal);
}

float3 LinearToST2084UE(float3 lin)
{
    const float m1 = 0.1593017578125; // = 2610. / 4096. * .25;
    const float m2 = 78.84375; // = 2523. / 4096. *  128;
    const float c1 = 0.8359375; // = 2392. / 4096. * 32 - 2413./4096.*32 + 1;
    const float c2 = 18.8515625; // = 2413. / 4096. * 32;
    const float c3 = 18.6875; // = 2392. / 4096. * 32;
    const float C = 10000.;

    float3 L = lin/C;
    float3 Lm = pow(L, float3(m1, m1, m1));
    float3 N1 = (c1 + c2 * Lm);
    float3 N2 = (1.0 + c3 * Lm);
    float3 N = N1 * (1.0 / N2);
    float3 P = pow(N, float3(m2, m2, m2));

    return P;
}

float3 ACES_Tonemapping(float3 color){
    float3x3 m1 = float3x3(
        0.59719, 0.07600, 0.02840,
        0.35458, 0.90834, 0.13383,
        0.04823, 0.01566, 0.83777
    );
    float3x3 m2 = float3x3(
        1.60475, -0.10208, -0.00327,
        -0.53108, 1.10813, -0.07276,
        -0.07367, -0.00605, 1.07602
    );
    float3 v = mul(m1, color);
    float3 a = v * (v + 0.0245786) - 0.000090537;
    float3 b = v * (0.983729 * v + 0.4329510) + 0.238081;
    return pow(clamp(mul(m2, (a / b)), 0.0, 1.0), float3(1.0 / 2.2, 1.0 / 2.2, 1.0 / 2.2));
}

float3 uncharted2Tonemap(float3 x) {
    float A = 0.15;
    float B = 0.50;
    float C = 0.10;
    float D = 0.20;
    float E = 0.02;
    float F = 0.30;
    float W = 11.2;
    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

float3 Uncharted2_Tonemapping(float3 color) {
    const float W = 11.2;
    float exposureBias = 2.0;
    float3 curr = uncharted2Tonemap(exposureBias * color);
    float3 whiteScale = 1.0 / uncharted2Tonemap(float3(W, W, W));
    return curr * whiteScale;
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

float3 rgb2ycocg(float3 rgb) {
    float Y  = 0.25 * rgb.r + 0.5 * rgb.g + 0.25 * rgb.b;
    float Co = 0.5 * rgb.r - 0.5 * rgb.b;
    float Cg = -0.25 * rgb.r + 0.5 * rgb.g - 0.25 * rgb.b;
    return float3(Y, Co, Cg);
}

float3 ycocg2rgb(float3 ycocg) {
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

#define CONST_FUNC_H
#endif