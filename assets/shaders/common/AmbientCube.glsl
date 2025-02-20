
// Sample the ambient cube using the Half-Life 2 algorithm
vec4 sampleAmbientCubeHL2(AmbientCube cube, vec3 normal, out float occlusion) {
    vec4 color = vec4(0.0);
    color += max(normal.x, 0.0) 	* cube.PosX;
    color += max(-normal.x, 0.0) 	* cube.NegX;
    color += max(normal.y, 0.0) 	* cube.PosY;
    color += max(-normal.y, 0.0) 	* cube.NegY;
    color += max(normal.z, 0.0) 	* cube.PosZ;
    color += max(-normal.z, 0.0) 	* cube.NegZ;
    color.w = cube.Info.y;
    return color;
}

// Interpolate between 8 probes
vec4 interpolateProbes(vec3 pos, vec3 normal) {
    // Early out if position is outside the probe grid
    if (pos.x < 0 || pos.y < 0 || pos.z < 0 ||
    pos.x > CUBE_SIZE || pos.y > CUBE_SIZE || pos.z > CUBE_SIZE) {
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
        int idx = probePos.z * CUBE_SIZE * CUBE_SIZE +
        probePos.y * CUBE_SIZE +
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
            probePos.x >= CUBE_SIZE || probePos.y >= CUBE_SIZE || probePos.z >= CUBE_SIZE)
            continue;

            int idx = probePos.z * CUBE_SIZE * CUBE_SIZE +
            probePos.y * CUBE_SIZE +
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
    vec4 indirectColor = totalWeight > 0.0 ? result / totalWeight : vec4(0.0);
    return indirectColor;
}