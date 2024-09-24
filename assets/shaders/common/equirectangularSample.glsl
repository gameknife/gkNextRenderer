#include "Const_Func.glsl" //pi consts

#ifndef equirectangularSample
// The helper for the equirectangular textures.
vec4 equirectangularSample(vec3 direction, float rotate)
{
	vec3 d = normalize(direction);
	vec2 t = vec2((atan(d.x, d.z) + M_PI * rotate) * M_1_OVER_TWO_PI, acos(d.y) * M_1_PI);

	return min( vec4(10,10,10,1), texture(TextureSamplers[Camera.SkyIdx], t));
}
		
#endif
