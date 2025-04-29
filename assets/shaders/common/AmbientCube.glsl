#include "UniformBufferObject.glsl"
#include "AmbientCubeCommon.glsl"

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
    color.w = unpackHalf2x16(cube.Lighting).x;
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
    color.w = unpackHalf2x16(cube.Lighting).x;
    return color;
}


AmbientCube FetchCube(ivec3 probePos, bool useFar)
{
    int idx = probePos.y * CUBE_SIZE_XY * CUBE_SIZE_XY +
    probePos.z * CUBE_SIZE_XY + probePos.x;
    return useFar ? FarCubes[idx] : Cubes[idx];
}

vec4 interpolateSkyProbes(vec3 pos, vec3 normal) {
    
    bool useFar = false;
    
    vec3 nearpos = (pos - CUBE_OFFSET) / CUBE_UNIT;
    vec3 farpos = (pos - CUBE_OFFSET_FAR) / CUBE_UNIT_FAR;
        
    pos = nearpos;
    
    // Early out if position is outside the probe grid
    if (nearpos.x < 0 || nearpos.y < 0 || nearpos.z < 0 ||
    nearpos.x > CUBE_SIZE_XY - 1 || nearpos.y > CUBE_SIZE_Z - 1 || nearpos.z > CUBE_SIZE_XY - 1) {
        useFar = true;
        pos = farpos;

        if (farpos.x < 0 || farpos.y < 0 || farpos.z < 0 ||
        farpos.x > CUBE_SIZE_XY - 1 || farpos.y > CUBE_SIZE_Z - 1 || farpos.z > CUBE_SIZE_XY - 1) {
            return vec4(0);
        }
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
        AmbientCube cube = FetchCube(probePos, useFar);
        if (cube.Active == 0 && cube.ExtInfo3 == 0) continue;

        float wx = offset.x == 0 ? (1.0 - frac.x) : frac.x;
        float wy = offset.y == 0 ? (1.0 - frac.y) : frac.y;
        float wz = offset.z == 0 ? (1.0 - frac.z) : frac.z;
        float weight = wx * wy * wz;

        float occlusion;
        vec4 sampleColor = sampleAmbientCubeHL2_Sky(cube, normal, occlusion);
        result += sampleColor * weight;
        totalWeight += weight;
    }
    
    // Normalize result by total weight
    vec4 indirectColor = totalWeight > 0.0 ? result / totalWeight : vec4(0.05);
    return indirectColor;
}

// Interpolate between 8 probes
vec4 interpolateProbes(vec3 pos, vec3 normal) {
    bool useFar = false;

    vec3 nearpos = (pos - CUBE_OFFSET) / CUBE_UNIT;
    vec3 farpos = (pos - CUBE_OFFSET_FAR) / CUBE_UNIT_FAR;

    pos = nearpos;

    // Early out if position is outside the probe grid
    if (nearpos.x < 0 || nearpos.y < 0 || nearpos.z < 0 ||
    nearpos.x > CUBE_SIZE_XY - 1 || nearpos.y > CUBE_SIZE_Z - 1 || nearpos.z > CUBE_SIZE_XY - 1) {
        useFar = true;
        pos = farpos;

        if (farpos.x < 0 || farpos.y < 0 || farpos.z < 0 ||
        farpos.x > CUBE_SIZE_XY - 1 || farpos.y > CUBE_SIZE_Z - 1 || farpos.z > CUBE_SIZE_XY - 1) {
            return vec4(0);
        }
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
        AmbientCube cube = FetchCube(probePos, useFar);
        if (cube.Active == 0 && cube.ExtInfo3 == 0) continue;

        float wx = offset.x == 0 ? (1.0 - frac.x) : frac.x;
        float wy = offset.y == 0 ? (1.0 - frac.y) : frac.y;
        float wz = offset.z == 0 ? (1.0 - frac.z) : frac.z;
        float weight = wx * wy * wz;

        float occlusion;
        vec4 sampleColor = sampleAmbientCubeHL2_Full(cube, normal, occlusion);
        result += sampleColor * weight;
        totalWeight += weight;
    }

    // Normalize result by total weight
    vec4 indirectColor = totalWeight > 0.0 ? result / totalWeight : vec4(0.05);
    return indirectColor;
}


// Interpolate between 8 probes
bool inSolid(vec3 pos, inout uint matId) {
    // Early out if position is outside the probe grid
    if (pos.x <= 0 || pos.y <= 0 || pos.z <= 0 ||
    pos.x > CUBE_SIZE_XY - 1 || pos.y > CUBE_SIZE_Z - 1 || pos.z > CUBE_SIZE_XY - 1) {
        return false;
    }
    
    ivec3 baseIdx = ivec3(floor(pos));

    AmbientCube cube = FetchCube(baseIdx, false);
    matId = cube.ExtInfo1;
    return cube.Active == 1 ? false : true;
}