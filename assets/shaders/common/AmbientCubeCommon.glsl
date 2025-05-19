#ifdef __cplusplus
#define HIGH_QUALITY 0
#else
#define HIGH_QUALITY 0
#endif

#define MAX_ILLUMINANCE 512.f

const vec3 cubeVectors[6] = {
vec3(0, 1, 0),
vec3(0, -1, 0),
vec3(0, 0, 1),
vec3(0, 0, -1),
vec3(1, 0, 0),
vec3(-1, 0, 0),
};
        
#if HIGH_QUALITY
const uint FACE_TRACING = 32;
#else
const uint FACE_TRACING = 16;
#endif

const vec2 grid4x4[16] = {
    vec2(-0.75, -0.75), vec2(-0.25, -0.75), vec2(0.25, -0.75), vec2(0.75, -0.75),
    vec2(-0.75, -0.25), vec2(-0.25, -0.25), vec2(0.25, -0.25), vec2(0.75, -0.25),
    vec2(-0.75,  0.25), vec2(-0.25,  0.25), vec2(0.25,  0.25), vec2(0.75,  0.25),
    vec2(-0.75,  0.75), vec2(-0.25,  0.75), vec2(0.25,  0.75), vec2(0.75,  0.75)
};

const vec2 grid5x5[25] = {
    vec2(-0.8, -0.8), vec2(-0.4, -0.8), vec2(0.0, -0.8), vec2(0.4, -0.8), vec2(0.8, -0.8),
    vec2(-0.8, -0.4), vec2(-0.4, -0.4), vec2(0.0, -0.4), vec2(0.4, -0.4), vec2(0.8, -0.4),
    vec2(-0.8,  0.0), vec2(-0.4,  0.0), vec2(0.0,  0.0), vec2(0.4,  0.0), vec2(0.8,  0.0),
    vec2(-0.8,  0.4), vec2(-0.4,  0.4), vec2(0.0,  0.4), vec2(0.4,  0.4), vec2(0.8,  0.4),
    vec2(-0.8,  0.8), vec2(-0.4,  0.8), vec2(0.0,  0.8), vec2(0.4,  0.8), vec2(0.8,  0.8)
};

uint packRGB10A2(vec4 color) {
    vec4 clamped = clamp( color / MAX_ILLUMINANCE, vec4(0.0f), vec4(1.0f) );

    uint r = uint(clamped.r * 1023.0f);
    uint g = uint(clamped.g * 1023.0f);
    uint b = uint(clamped.b * 1023.0f);
    uint a = uint(clamped.a * 3.0f);

    return r | (g << 10) | (b << 20) | (a << 30);
}

vec4 unpackRGB10A2(uint packed) {
    float r = float((packed) & 0x3FF) / 1023.0f;
    float g = float((packed >> 10) & 0x3FF) / 1023.0f;
    float b = float((packed >> 20) & 0x3FF) / 1023.0f;
    //float a = packed & 0x3;

    return vec4(r,g,b,0.0) * MAX_ILLUMINANCE;
}

uint PackColor(vec4 source) {
    return packRGB10A2(source);
}

vec4 UnpackColor(uint packed) {
    return unpackRGB10A2(packed);
}

uint LerpPackedColor(uint c0, uint c1, float t) {
    // Extract RGBA components (8 bits each)
    vec4 color0 = UnpackColor(c0);
    vec4 color1 = UnpackColor(c1);
    // Pack back to uint32
    return PackColor(mix(color0, color1, t));
}

uint LerpPackedColorAlt(uint c0, vec4 c1, float t) {
    // Extract RGBA components (8 bits each)
    vec4 color0 = UnpackColor(c0);
    vec4 color1 = c1;
    // Pack back to uint32
    return PackColor(mix(color0, color1, t));
}

vec4 sampleAmbientCubeHL2_DI(AmbientCube cube, vec3 normal) {
    vec4 color = vec4(0.0);
    float sum = 0.0;

    float wx = max(normal.x, 0.0f);
    float wnx = max(-normal.x, 0.0f);
    float wy = max(normal.y, 0.0f);
    float wny = max(-normal.y, 0.0f);
    float wz = max(normal.z, 0.0f);
    float wnz = max(-normal.z, 0.0f);

    sum = wx + wnx + wy + wny + wz + wnz;

    color += wx *   UnpackColor(cube.PosX_D);
    color += wnx *  UnpackColor(cube.NegX_D);
    color += wy *   UnpackColor(cube.PosY_D);
    color += wny *  UnpackColor(cube.NegY_D);
    color += wz *   UnpackColor(cube.PosZ_D);
    color += wnz *  UnpackColor(cube.NegZ_D);

    // 归一化处理
    color *= (sum > 0.0) ? (1.0 / sum) : 1.0;
    color.w = unpackHalf2x16(cube.Lighting).x;
    return color;
}
