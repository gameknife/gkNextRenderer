
#define MAX_ILLUMINANCE 10.f

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
    color.w = cube.Info.y;
    return color;
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
        int idx = probePos.y * CUBE_SIZE_XY * CUBE_SIZE_XY +
        probePos.z * CUBE_SIZE_XY +
        probePos.x;

        AmbientCube cube = Cubes[idx];
        if (cube.Info.x != 1) continue;

        float wx = offset.x == 0 ? (1.0 - frac.x) : frac.x;
        float wy = offset.y == 0 ? (1.0 - frac.y) : frac.y;
        float wz = offset.z == 0 ? (1.0 - frac.z) : frac.z;
        float weight = wx * wy * wz;

        float occlusion;
        vec4 sampleColor = sampleAmbientCubeHL2(cube, normal, occlusion);
        result += sampleColor * weight;
        totalWeight += weight;
    }

    // If no valid cubes found, try expanded search
    if (totalWeight <= 0.0) {
        for (int i = 0; i < 8; i++) {
            ivec3 offset = ivec3(
            (i & 1) * 2 - 1,
            ((i >> 1) & 1) * 2 - 1,
            ((i >> 2) & 1) * 2 - 1
            );

            ivec3 probePos = baseIdx + offset;
            if (probePos.x < 0 || probePos.y < 0 || probePos.z < 0 ||
            probePos.x >= CUBE_SIZE_XY || probePos.z >= CUBE_SIZE_XY || probePos.y >= CUBE_SIZE_Z)
            continue;

            int idx = probePos.y * CUBE_SIZE_XY * CUBE_SIZE_XY +
            probePos.z * CUBE_SIZE_XY +
            probePos.x;

            AmbientCube cube = Cubes[idx];
            if (cube.Info.x != 1) continue;
            
            float dist = length(vec3(offset));
            float weight = 1.0 / (dist * dist);

            float occlusion;
            vec4 sampleColor = sampleAmbientCubeHL2(cube, normal, occlusion);
            result += sampleColor * weight;
            totalWeight += weight;
        }
    }

    // Normalize result by total weight
    vec4 indirectColor = totalWeight > 0.0 ? result / totalWeight : vec4(0.05);
    return indirectColor;
}