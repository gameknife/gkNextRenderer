#define HIGH_QUALITY 0
#define MAX_ILLUMINANCE 512.f

const vec3 hemisphereVectors32[32] = {
vec3(0.06380871270368209, -0.16411674977923174, 0.984375),
vec3(-0.2713456981395, 0.13388146427413833, 0.953125),
vec3(0.3720439325965911, 0.1083041854826636, 0.921875),
vec3(-0.2360905314110366, -0.38864941831045413, 0.890625),
vec3(-0.0994527932145428, 0.5015812509422829, 0.859375),
vec3(0.4518001015172772, -0.3317915801282155, 0.828125),
vec3(-0.6006110862696151, -0.06524229782152816, 0.796875),
vec3(0.4246400102205648, 0.4832175711777031, 0.765625),
vec3(0.014025106917771066, -0.6785990390141627, 0.734375),
vec3(-0.4910499646725058, 0.5142812135107899, 0.703125),
vec3(0.7390090634100329, -0.04949331846649647, 0.671875),
vec3(-0.5995855814426192, -0.47968400004702705, 0.640625),
vec3(0.12194313936463708, 0.7834487731414841, 0.609375),
vec3(0.4520746755881747, -0.6792642873483389, 0.578125),
vec3(-0.8128288753565273, 0.2005915096948101, 0.546875),
vec3(0.7520560261769773, 0.41053939258723227, 0.515625),
vec3(-0.28306625928882, -0.8278009134008216, 0.48437499999999994),
vec3(-0.357091373373129, 0.8168007623879232, 0.4531249999999999),
vec3(0.8289528112710368, -0.36723115480694823, 0.42187500000000006),
vec3(-0.8724752793478753, -0.29359665580835004, 0.39062500000000006),
vec3(0.45112649883473616, 0.8169054360353547, 0.35937499999999994),
vec3(0.22185204734195418, -0.9182132941017481, 0.328125),
vec3(-0.792362708282336, 0.5329414347735422, 0.296875),
vec3(0.9533182286148584, 0.1436235160606669, 0.26562499999999994),
vec3(-0.6110028678351297, -0.756137457657169, 0.23437499999999994),
vec3(-0.06066310322190489, 0.9772718261990818, 0.20312499999999992),
vec3(0.7091634641369421, -0.683773475288631, 0.17187500000000008),
vec3(-0.9897399665274648, 0.025286518803760413, 0.14062500000000008),
vec3(0.749854715318357, 0.6524990538612495, 0.1093750000000001),
vec3(-0.11249484107422018, -0.9905762944401031, 0.0781250000000001),
vec3(-0.587325250956154, 0.8079924405365998, 0.046875),
vec3(0.9798239737002217, -0.19925069620281746, 0.01562500000000002)
};
const vec3 hemisphereVectors16[16] = {
vec3(0.0898831725494359, -0.23118056318048955, 0.96875),
vec3(-0.3791079112581037, 0.18705114039085075, 0.90625),
vec3(0.5153445316614019, 0.15001983597741456, 0.84375),
vec3(-0.3240808043433482, -0.5334979566560387, 0.78125),
vec3(-0.1352243320429325, 0.6819918016542008, 0.71875),
vec3(0.6081648613009205, -0.4466222553554984, 0.65625),
vec3(-0.7999438572571327, -0.08689512492988453, 0.59375),
vec3(0.559254806831153, 0.6364019944471022, 0.53125),
vec3(0.018252552906059358, -0.8831422772194816, 0.46875000000000006),
vec3(-0.6310280835640938, 0.66088160456577, 0.40624999999999994),
vec3(0.9369622623684265, -0.06275074818231247, 0.34374999999999994),
vec3(-0.7493392047328667, -0.5994907787033216, 0.2812499999999999),
vec3(0.1500724796134238, 0.9641715036043528, 0.21875),
vec3(0.5472431702110503, -0.8222596002220706, 0.15624999999999997),
vec3(-0.9665972247046685, 0.2385387655984508, 0.09375000000000008),
vec3(0.8773063928879596, 0.47891223673854594, 0.03125000000000001)
};

#if HIGH_QUALITY
const uint FACE_TRACING = 32;
#else
const uint FACE_TRACING = 16;
#endif

uint packRGB10A2(vec4 color) {
    vec4 clamped = clamp( color / MAX_ILLUMINANCE, vec4(0.0f), vec4(1.0f) );

    uint r = uint(clamped.r * 1023.0f);
    uint g = uint(clamped.g * 1023.0f);
    uint b = uint(clamped.b * 1023.0f);
    uint a = uint(clamped.a * 3.0f);

    return r | (g << 10) | (b << 20) | (a << 30);
}

vec4 unpackRGB10A2(uint packed) {
    float r = float((packed) & 0x3FF) / 1023.0;
    float g = float((packed >> 10) & 0x3FF) / 1023.0;
    float b = float((packed >> 20) & 0x3FF) / 1023.0;
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

vec4 sampleAmbientCubeHL2(AmbientCube cube, vec3 normal, out float occlusion) {
    vec4 color = vec4(0.0);
    float sum = 0.0;
    
    float wx = max(normal.x, 0.0);
    float wnx = max(-normal.x, 0.0);
    float wy = max(normal.y, 0.0);
    float wny = max(-normal.y, 0.0);
    float wz = max(normal.z, 0.0);
    float wnz = max(-normal.z, 0.0);
    
    sum = wx + wnx + wy + wny + wz + wnz;
    
    color += wx * UnpackColor(cube.PosX);
    color += wnx * UnpackColor(cube.NegX);
    color += wy * UnpackColor(cube.PosY);
    color += wny * UnpackColor(cube.NegY);
    color += wz * UnpackColor(cube.PosZ);
    color += wnz * UnpackColor(cube.NegZ);
    
    // 归一化处理
    color.xyz *= (sum > 0.0) ? (1.0 / sum) : 1.0;
    color.w = unpackHalf2x16(cube.Info.y).x;
    return color;
}

vec4 sampleAmbientCubeHL2_DI(AmbientCube cube, vec3 normal, out float occlusion) {
    vec4 color = vec4(0.0);
    float sum = 0.0;

    float wx = max(normal.x, 0.0);
    float wnx = max(-normal.x, 0.0);
    float wy = max(normal.y, 0.0);
    float wny = max(-normal.y, 0.0);
    float wz = max(normal.z, 0.0);
    float wnz = max(-normal.z, 0.0);

    sum = wx + wnx + wy + wny + wz + wnz;

    color += wx *   UnpackColor(cube.PosX_D);
    color += wnx *  UnpackColor(cube.NegX_D);
    color += wy *   UnpackColor(cube.PosY_D);
    color += wny *  UnpackColor(cube.NegY_D);
    color += wz *   UnpackColor(cube.PosZ_D);
    color += wnz *  UnpackColor(cube.NegZ_D);

    // 归一化处理
    color.xyz *= (sum > 0.0) ? (1.0 / sum) : 1.0;
    color.w = unpackHalf2x16(cube.Info.y).x;
    return color;
}

vec4 sampleAmbientCubeHL2_II(AmbientCube cube, vec3 normal, out float occlusion) {
    vec4 color = vec4(0.0);
    float sum = 0.0;

    float wx = max(normal.x, 0.0);
    float wnx = max(-normal.x, 0.0);
    float wy = max(normal.y, 0.0);
    float wny = max(-normal.y, 0.0);
    float wz = max(normal.z, 0.0);
    float wnz = max(-normal.z, 0.0);

    sum = wx + wnx + wy + wny + wz + wnz;

    color += wx *   UnpackColor(cube.PosX);
    color += wnx *  UnpackColor(cube.NegX);
    color += wy *   UnpackColor(cube.PosY);
    color += wny *  UnpackColor(cube.NegY);
    color += wz *   UnpackColor(cube.PosZ);
    color += wnz *  UnpackColor(cube.NegZ);

    // 归一化处理
    color.xyz *= (sum > 0.0) ? (1.0 / sum) : 1.0;
    color.w = unpackHalf2x16(cube.Info.y).x;
    return color;
}

vec4 sampleAmbientCubeHL2_Sky(AmbientCube cube, vec3 normal, out float occlusion) {
    vec4 color = vec4(0.0);
    float sum = 0.0;

    float wx = max(normal.x, 0.0);
    float wnx = max(-normal.x, 0.0);
    float wy = max(normal.y, 0.0);
    float wny = max(-normal.y, 0.0);
    float wz = max(normal.z, 0.0);
    float wnz = max(-normal.z, 0.0);

    sum = wx + wnx + wy + wny + wz + wnz;

    color += wx *   UnpackColor(cube.PosX_S);
    color += wnx *  UnpackColor(cube.NegX_S);
    color += wy *   UnpackColor(cube.PosY_S);
    color += wny *  UnpackColor(cube.NegY_S);
    color += wz *   UnpackColor(cube.PosZ_S);
    color += wnz *  UnpackColor(cube.NegZ_S);

    color += wx *   UnpackColor(cube.PosX);
    color += wnx *  UnpackColor(cube.NegX);
    color += wy *   UnpackColor(cube.PosY);
    color += wny *  UnpackColor(cube.NegY);
    color += wz *   UnpackColor(cube.PosZ);
    color += wnz *  UnpackColor(cube.NegZ);

    // 归一化处理
    color.xyz *= (sum > 0.0) ? (1.0 / sum) : 1.0;
    color.w = unpackHalf2x16(cube.Info.y).x;
    return color;
}

vec4 sampleAmbientCubeHL2_Full(AmbientCube cube, vec3 normal, out float occlusion) {
    vec4 color = vec4(0.0);
    float sum = 0.0;

    float wx = max(normal.x, 0.0);
    float wnx = max(-normal.x, 0.0);
    float wy = max(normal.y, 0.0);
    float wny = max(-normal.y, 0.0);
    float wz = max(normal.z, 0.0);
    float wnz = max(-normal.z, 0.0);

    sum = wx + wnx + wy + wny + wz + wnz;

    color += wx *   UnpackColor(cube.PosX_D);
    color += wnx *  UnpackColor(cube.NegX_D);
    color += wy *   UnpackColor(cube.PosY_D);
    color += wny *  UnpackColor(cube.NegY_D);
    color += wz *   UnpackColor(cube.PosZ_D);
    color += wnz *  UnpackColor(cube.NegZ_D);

    color += wx *   UnpackColor(cube.PosX);
    color += wnx *  UnpackColor(cube.NegX);
    color += wy *   UnpackColor(cube.PosY);
    color += wny *  UnpackColor(cube.NegY);
    color += wz *   UnpackColor(cube.PosZ);
    color += wnz *  UnpackColor(cube.NegZ);

    // 归一化处理
    color.xyz *= (sum > 0.0) ? (1.0 / sum) : 1.0;
    color.w = unpackHalf2x16(cube.Info.y).x;
    return color;
}


AmbientCube FetchCube(ivec3 probePos)
{
    int idx = probePos.y * CUBE_SIZE_XY * CUBE_SIZE_XY +
    probePos.z * CUBE_SIZE_XY + probePos.x;
    return Cubes[idx];
}

vec4 interpolateDIProbes(vec3 pos, vec3 normal) {
    // Early out if position is outside the probe grid
    if (pos.x < 0 || pos.y < 0 || pos.z < 0 ||
    pos.x > CUBE_SIZE_XY || pos.y > CUBE_SIZE_Z || pos.z > CUBE_SIZE_XY) {
        return vec4(1.0);
    }

    // Get the base indices and fractional positions
    ivec3 baseIdx = ivec3(floor(pos));
    vec3 frac = fract(pos);

    // Trilinear interpolation between 8 nearest probes
    // Try immediate neighbors first
    float totalWeight = 0.0;
    vec4 result = vec4(0.0);

    for (int i = 0; i < 8; i++) {
        ivec3 offset = ivec3(
        i & 1,
        (i >> 1) & 1,
        (i >> 2) & 1
        );

        ivec3 probePos = baseIdx + offset;
        AmbientCube cube = FetchCube(probePos);
        if (cube.Info.x != 1) continue;

        float wx = offset.x == 0 ? (1.0 - frac.x) : frac.x;
        float wy = offset.y == 0 ? (1.0 - frac.y) : frac.y;
        float wz = offset.z == 0 ? (1.0 - frac.z) : frac.z;
        float weight = wx * wy * wz;

        float occlusion;
        vec4 sampleColor = sampleAmbientCubeHL2_DI(cube, normal, occlusion);
        result += sampleColor * weight;
        totalWeight += weight;
    }

    // up cascade
    if(totalWeight <= 0.0) {
        baseIdx = baseIdx - ivec3(1,1,1);
        for (int i = 0; i < 8; i++) {
            ivec3 offset = ivec3(
            i & 1,
            (i >> 1) & 1,
            (i >> 2) & 1
            );

            ivec3 probePos = baseIdx + offset * 3;
            AmbientCube cube = FetchCube(probePos);
            if (cube.Info.x != 1) continue;

            float wx = offset.x == 0 ? (1.0 - frac.x) : frac.x;
            float wy = offset.y == 0 ? (1.0 - frac.y) : frac.y;
            float wz = offset.z == 0 ? (1.0 - frac.z) : frac.z;
            float weight = wx * wy * wz;

            float occlusion;
            vec4 sampleColor = sampleAmbientCubeHL2_DI(cube, normal, occlusion);
            result += sampleColor * weight;
            totalWeight += weight;
        }
    }

    // Normalize result by total weight
    vec4 indirectColor = totalWeight > 0.0 ? result / totalWeight : vec4(0.05);
    return indirectColor;
}

vec4 interpolateIIProbes(vec3 pos, vec3 normal) {
    // Early out if position is outside the probe grid
    if (pos.x < 0 || pos.y < 0 || pos.z < 0 ||
    pos.x > CUBE_SIZE_XY || pos.y > CUBE_SIZE_Z || pos.z > CUBE_SIZE_XY) {
        return vec4(1.0);
    }

    // Get the base indices and fractional positions
    ivec3 baseIdx = ivec3(floor(pos));
    vec3 frac = fract(pos);

    // Trilinear interpolation between 8 nearest probes
    // Try immediate neighbors first
    float totalWeight = 0.0;
    vec4 result = vec4(0.0);

    for (int i = 0; i < 8; i++) {
        ivec3 offset = ivec3(
        i & 1,
        (i >> 1) & 1,
        (i >> 2) & 1
        );

        ivec3 probePos = baseIdx + offset;
        AmbientCube cube = FetchCube(probePos);
        if (cube.Info.x != 1) continue;

        float wx = offset.x == 0 ? (1.0 - frac.x) : frac.x;
        float wy = offset.y == 0 ? (1.0 - frac.y) : frac.y;
        float wz = offset.z == 0 ? (1.0 - frac.z) : frac.z;
        float weight = wx * wy * wz;

        float occlusion;
        vec4 sampleColor = sampleAmbientCubeHL2_II(cube, normal, occlusion);
        result += sampleColor * weight;
        totalWeight += weight;
    }

    // up cascade
    if(totalWeight <= 0.0) {
        baseIdx = baseIdx - ivec3(1,1,1);
        for (int i = 0; i < 8; i++) {
            ivec3 offset = ivec3(
            i & 1,
            (i >> 1) & 1,
            (i >> 2) & 1
            );

            ivec3 probePos = baseIdx + offset * 3;
            AmbientCube cube = FetchCube(probePos);
            if (cube.Info.x != 1) continue;

            float wx = offset.x == 0 ? (1.0 - frac.x) : frac.x;
            float wy = offset.y == 0 ? (1.0 - frac.y) : frac.y;
            float wz = offset.z == 0 ? (1.0 - frac.z) : frac.z;
            float weight = wx * wy * wz;

            float occlusion;
            vec4 sampleColor = sampleAmbientCubeHL2_II(cube, normal, occlusion);
            result += sampleColor * weight;
            totalWeight += weight;
        }
    }

    // Normalize result by total weight
    vec4 indirectColor = totalWeight > 0.0 ? result / totalWeight : vec4(0.05);
    return indirectColor;
}


vec4 interpolateSkyProbes(vec3 pos, vec3 normal) {
    // Early out if position is outside the probe grid
    if (pos.x < 0 || pos.y < 0 || pos.z < 0 ||
    pos.x > CUBE_SIZE_XY || pos.y > CUBE_SIZE_Z || pos.z > CUBE_SIZE_XY) {
        return vec4(1.0);
    }

    // Get the base indices and fractional positions
    ivec3 baseIdx = ivec3(floor(pos));
    vec3 frac = fract(pos);

    // Trilinear interpolation between 8 nearest probes
    // Try immediate neighbors first
    float totalWeight = 0.0;
    vec4 result = vec4(0.0);

    for (int i = 0; i < 8; i++) {
        ivec3 offset = ivec3(
        i & 1,
        (i >> 1) & 1,
        (i >> 2) & 1
        );

        ivec3 probePos = baseIdx + offset;
        AmbientCube cube = FetchCube(probePos);
        if (cube.Info.x != 1) continue;

        float wx = offset.x == 0 ? (1.0 - frac.x) : frac.x;
        float wy = offset.y == 0 ? (1.0 - frac.y) : frac.y;
        float wz = offset.z == 0 ? (1.0 - frac.z) : frac.z;
        float weight = wx * wy * wz;

        float occlusion;
        vec4 sampleColor = sampleAmbientCubeHL2_Sky(cube, normal, occlusion);
        result += sampleColor * weight;
        totalWeight += weight;
    }

    // up cascade
    if(totalWeight <= 0.0) {
        baseIdx = baseIdx - ivec3(1,1,1);
        for (int i = 0; i < 8; i++) {
            ivec3 offset = ivec3(
            i & 1,
            (i >> 1) & 1,
            (i >> 2) & 1
            );

            ivec3 probePos = baseIdx + offset * 3;
            AmbientCube cube = FetchCube(probePos);
            if (cube.Info.x != 1) continue;

            float wx = offset.x == 0 ? (1.0 - frac.x) : frac.x;
            float wy = offset.y == 0 ? (1.0 - frac.y) : frac.y;
            float wz = offset.z == 0 ? (1.0 - frac.z) : frac.z;
            float weight = wx * wy * wz;

            float occlusion;
            vec4 sampleColor = sampleAmbientCubeHL2_Sky(cube, normal, occlusion);
            result += sampleColor * weight;
            totalWeight += weight;
        }
    }

    // Normalize result by total weight
    vec4 indirectColor = totalWeight > 0.0 ? result / totalWeight : vec4(0.05);
    return indirectColor;
}

// Interpolate between 8 probes
vec4 interpolateProbes(vec3 pos, vec3 normal) {
    // Early out if position is outside the probe grid
    if (pos.x < 0 || pos.y < 0 || pos.z < 0 ||
    pos.x > CUBE_SIZE_XY || pos.y > CUBE_SIZE_Z || pos.z > CUBE_SIZE_XY) {
        return vec4(1.0);
    }

    // Get the base indices and fractional positions
    ivec3 baseIdx = ivec3(floor(pos));
    vec3 frac = fract(pos);

    // Trilinear interpolation between 8 nearest probes
    // Try immediate neighbors first
    float totalWeight = 0.0;
    vec4 result = vec4(0.0);

    for (int i = 0; i < 8; i++) {
        ivec3 offset = ivec3(
        i & 1,
        (i >> 1) & 1,
        (i >> 2) & 1
        );

        ivec3 probePos = baseIdx + offset;
        AmbientCube cube = FetchCube(probePos);
        if (cube.Info.x != 1) continue;

        float wx = offset.x == 0 ? (1.0 - frac.x) : frac.x;
        float wy = offset.y == 0 ? (1.0 - frac.y) : frac.y;
        float wz = offset.z == 0 ? (1.0 - frac.z) : frac.z;
        float weight = wx * wy * wz;

        float occlusion;
        vec4 sampleColor = sampleAmbientCubeHL2_Full(cube, normal, occlusion);
        result += sampleColor * weight;
        totalWeight += weight;
    }
    
    // up cascade
    if(totalWeight <= 0.0) {
        baseIdx = baseIdx - ivec3(1,1,1);
        for (int i = 0; i < 8; i++) {
            ivec3 offset = ivec3(
            i & 1,
            (i >> 1) & 1,
            (i >> 2) & 1
            );

            ivec3 probePos = baseIdx + offset * 3;
            AmbientCube cube = FetchCube(probePos);
            if (cube.Info.x != 1) continue;

            float wx = offset.x == 0 ? (1.0 - frac.x) : frac.x;
            float wy = offset.y == 0 ? (1.0 - frac.y) : frac.y;
            float wz = offset.z == 0 ? (1.0 - frac.z) : frac.z;
            float weight = wx * wy * wz;

            float occlusion;
            vec4 sampleColor = sampleAmbientCubeHL2_Full(cube, normal, occlusion);
            result += sampleColor * weight;
            totalWeight += weight;
        }
    }

    // Normalize result by total weight
    vec4 indirectColor = totalWeight > 0.0 ? result / totalWeight : vec4(0.05);
    return indirectColor;
}
