#include "Const_Func.glsl" //pi consts

#ifndef equirectangularSample
// The helper for the equirectangular textures.
vec4 equirectangularSample(vec3 direction, float rotate)
{
	vec3 d = normalize(direction);
	vec2 t = vec2((atan(d.x, d.z) + M_PI * rotate) * M_1_OVER_TWO_PI, acos(d.y) * M_1_PI);

	return min( vec4(10,10,10,1), texture(TextureSamplers[Camera.SkyIdx], t));
}
		
vec4 equirectangularSampleBlur(vec3 direction, float rotate, float gap)
{
	vec3 d = normalize(direction);
	vec2 t = vec2((atan(d.x, d.z) + M_PI * rotate) * M_1_OVER_TWO_PI, acos(d.y) * M_1_PI);
	
	vec4 avgColor = vec4(0);
	float weight = 0.0;
	
	// Sample 3x3 grid with configurable gap
	for (int y = -1; y <= 1; y++) {
		for (int x = -1; x <= 1; x++) {
			// Apply distance-based weight (center sample gets more weight)
			float w = 1.0 / (1.0 + abs(x) + abs(y));
			vec2 offset = vec2(x, y) * gap;
			vec2 samplePos = t + offset;
			
			// Handle wrapping for longitude (x coordinate)
			samplePos.x = mod(samplePos.x, 1.0);
			
			// Clamp latitude (y coordinate) to valid range
			samplePos.y = clamp(samplePos.y, 0.0, 1.0);
			
			avgColor += texture(TextureSamplers[Camera.SkyIdx], samplePos) * w;
			weight += w;
		}
	}
	
	avgColor /= weight;
	return min(vec4(10,10,10,1), avgColor);
}
#endif
