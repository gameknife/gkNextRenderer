#include "PreProcessor.slang"

// Tonemapping curves split out of ConstFunc.slang.
// Provides: LinearToST2084UE (HDR PQ), CODTonemapping, GT_Tonemapping,
// ACES_Tonemapping, Uncharted2_Tonemapping. These are pure functions that
// operate on linear-light HDR input and return display-mapped output.

public float3 LinearSrgbToBt2020(float3 color)
{
    color = max(color, float3(0.0, 0.0, 0.0));
    return float3(
        dot(color, float3(0.6274039, 0.3292830, 0.0433131)),
        dot(color, float3(0.0690973, 0.9195404, 0.0113623)),
        dot(color, float3(0.0163914, 0.0880133, 0.8955953)));
}

public float3 LinearToST2084UE(float3 lin)
{
    const float m1 = 0.1593017578125; // = 2610. / 4096. * .25;
    const float m2 = 78.84375; // = 2523. / 4096. *  128;
    const float c1 = 0.8359375; // = 2392. / 4096. * 32 - 2413./4096.*32 + 1;
    const float c2 = 18.8515625; // = 2413. / 4096. * 32;
    const float c3 = 18.6875; // = 2392. / 4096. * 32;
    const float C = 10000.;

    float3 L = max(lin, float3(0.0, 0.0, 0.0)) / C;
    float3 Lm = pow(L, float3(m1, m1, m1));
    float3 N1 = (c1 + c2 * Lm);
    float3 N2 = (1.0 + c3 * Lm);
    float3 N = N1 * (1.0 / N2);
    float3 P = pow(N, float3(m2, m2, m2));

    return P;
}

public float3 LinearSrgbToHdr10(float3 linearSrgbNits)
{
    return LinearToST2084UE(LinearSrgbToBt2020(linearSrgbNits));
}

public float3 SceneColorToLinearSrgbNits(float3 color, float paperWhiteNit)
{
    float3 mapped = GT_Tonemapping(color / 2000.0) * 2000.0;
    return mapped * paperWhiteNit / 230.0;
}

public float3 EncodeHdrOutput(float3 color, float paperWhiteNit, uint outputMode)
{
    float3 linearSrgbNits = SceneColorToLinearSrgbNits(color, paperWhiteNit);
    if (outputMode == HDR_OUTPUT_MODE_EXTENDED_SRGB_LINEAR)
    {
        return linearSrgbNits / 203.0;
    }
    return LinearSrgbToHdr10(linearSrgbNits);
}

public float3 CODTonemapping(float3 color)
{
    float a = 0.22;
    float b = 0.30;
    float c = 0.10;
    float d = 0.20;
    float e = 0.01;
    float f = 0.30;
    float3 mapped = ((color * (a * color + c * b) + d * e) / (color * (a * color + b) + d * f)) - e / f;
    return saturate(mapped);
}

static const float e = 2.71828;

float W_f(float x, float e0, float e1) {
    if (x <= e0)
        return 0;
    if (x >= e1)
        return 1;
    float a = (x - e0) / (e1 - e0);
    return a * a * (3 - 2 * a);
}
float H_f(float x, float e0, float e1) {
    if (x <= e0)
        return 0;
    if (x >= e1)
        return 1;
    return (x - e0) / (e1 - e0);
}

float GranTurismoTonemapper(float x) {
    float P = 1;  // max output
    float a = 0.7;  // contrast, default 1.0
    float m = 0.22; // linear section start, 0.22
    float l = 0.4; // linear section length, 0.4
    float c = 1.33;
    float b = 0;
    float l0 = (P - m) * l / a;
    float L0 = m - m / a;
    float L1 = m + (1 - m) / a;
    float L_x = m + a * (x - m);
    float T_x = m * pow(x / m, c) + b;
    float S0 = m + l0;
    float S1 = m + a * l0;
    float C2 = a * P / (P - S1);
    float S_x = P - (P - S1) * pow(e, -(C2 * (x - S0) / P));
    float w0_x = 1 - W_f(x, 0, m);
    float w2_x = H_f(x, m + l0, m + l0);
    float w1_x = 1 - w0_x - w2_x;
    float f_x = T_x * w0_x + L_x * w1_x + S_x * w2_x;
    return f_x;
}

public float3 GT_Tonemapping(float3 x)
{
    return float3(GranTurismoTonemapper(x.r), GranTurismoTonemapper(x.g), GranTurismoTonemapper(x.b));
}

public float3 ACES_Tonemapping(float3 color){
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

public float3 Uncharted2_Tonemapping(float3 color) {
    const float W = 11.2;
    float exposureBias = 2.0;
    float3 curr = uncharted2Tonemap(exposureBias * color);
    float3 whiteScale = 1.0 / uncharted2Tonemap(float3(W, W, W));
    return curr * whiteScale;
}
