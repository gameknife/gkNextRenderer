#ifndef SAMPLEIBL_H_

vec3 EvaluateSH(float SHCoefficients[3][9], vec3 normal, float rotate) {
    // Apply rotation around Y-axis (0 to 2 maps to 0 to 360 degrees)
	float angle = rotate * 3.14159265358979323846f;
	float cosAngle = cos(angle);
	float sinAngle = sin(angle);
	
	// Rotate the normal vector around Y-axis
	vec3 rotatedNormal = vec3(
		normal.x * cosAngle + normal.z * sinAngle,
		normal.y,
		-normal.x * sinAngle + normal.z * cosAngle
	);
	
	// SH basis function evaluation
	const float SH_C0 = 0.282095f;
	const float SH_C1 = 0.488603f;
	const float SH_C2 = 1.092548f;
	const float SH_C3 = 0.315392f;
	const float SH_C4 = 0.546274f;
	
	float basis[9];
	basis[0] = SH_C0;
	basis[1] = -SH_C1 * rotatedNormal.y;
	basis[2] = SH_C1 * rotatedNormal.z;
	basis[3] = -SH_C1 * rotatedNormal.x;
	basis[4] = SH_C2 * rotatedNormal.x * rotatedNormal.y;
	basis[5] = -SH_C2 * rotatedNormal.y * rotatedNormal.z;
	basis[6] = SH_C3 * (3.f * rotatedNormal.y * rotatedNormal.y - 1.0f);
	basis[7] = -SH_C2 * rotatedNormal.x * rotatedNormal.z;
	basis[8] = SH_C4 * (rotatedNormal.x * rotatedNormal.x - rotatedNormal.z * rotatedNormal.z);
	
	vec3 color = vec3(0.0);
	for (int i = 0; i < 9; ++i) {
	color.r += SHCoefficients[0][i] * basis[i];
	color.g += SHCoefficients[1][i] * basis[i];
	color.b += SHCoefficients[2][i] * basis[i];
	}
	
	return color;
}

#ifdef __cplusplus
vec4 SampleIBL(uint skyIdx, vec3 direction, float rotate, float roughness)
{
	vec3 rayColor = EvaluateSH(HDRSHs[skyIdx].coefficients, direction, 1.0f - rotate);
	return vec4(rayColor, 1.0);
}
#else
vec4 SampleIBL(uint skyIdx, vec3 direction, float rotate, float roughness)
{
	 if(roughness > 0.6f)
	 {
		  vec3 rayColor = EvaluateSH(HDRSHs[skyIdx].coefficients, direction, 1.0 - rotate);
		  return vec4(rayColor * 1.0f, 1.0);
	 }
	 vec3 d = normalize(direction);
	 vec2 t = vec2((atan(d.x, d.z) + M_PI * rotate) * M_1_OVER_TWO_PI, acos(d.y) * M_1_PI);

	 // Sample with explicit LOD based on roughness
	 return min(vec4(10,10,10,1), texture(TextureSamplers[skyIdx], t));
}
#endif

#define SAMPLEIBL_H_
#endif
