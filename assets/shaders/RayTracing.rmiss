#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_ray_tracing : require
#include "RayPayload.glsl"
#include "UniformBufferObject.glsl"

layout(binding = 3) readonly uniform UniformBufferObjectStruct { UniformBufferObject Camera; };
layout(binding = 8) uniform sampler2D[] TextureSamplers;

layout(location = 0) rayPayloadInEXT RayPayload Ray;

// The helper for the equirectangular textures.
vec4 equirectangularSample(vec3 direction, float rotate)
{
    const float pi = 3.1415926535897932384626433832795;
    vec3 d = normalize(direction);
    vec2 t = vec2((atan(d.x, d.z) + pi * rotate) / (2.f * pi), acos(d.y) / pi);

    return min( vec4(10,10,10,1), texture(TextureSamplers[0], t));
}

void main()
{
	Ray.GBuffer = vec4(0,1,0,0);
	Ray.Albedo = vec4(1,1,1,1);
	Ray.primitiveId = 999;
	
	if (Camera.HasSky)
	{
		// Sky color
        const vec3 rayDirection = normalize(gl_WorldRayDirectionEXT);
		const float t = 0.5*(rayDirection.y + 1);
        //const vec3 skyColor = mix(vec3(1.0), vec3(0.5, 0.7, 1.0), t);
		const vec3 skyColor = equirectangularSample(rayDirection, Camera.SkyRotation).rgb * 4.0;

        Ray.Attenuation = skyColor;
		Ray.Distance = -1;
		Ray.EmitColor = vec4(skyColor, -1);
		Ray.pdf = 1.0;
	}
	else
	{
		Ray.Attenuation = vec3(0);
		Ray.Distance = -1;
		Ray.EmitColor = vec4(0);
		Ray.pdf = 1.0;
	}
}
