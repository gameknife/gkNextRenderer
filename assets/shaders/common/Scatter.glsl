#extension GL_EXT_nonuniform_qualifier : require

#include "Random.glsl"
#include "RayPayload.glsl"
#include "common/Const_Func.glsl"
#include "common/ColorFunc.glsl"
#include "common/GGXSample.glsl"

#ifndef scatter_inc
#define scatter_inc

#define cos_0_5degree 0.99996192306417128873735516482698f

void ScatterDiffuseLight(inout RayPayload ray, const Material m, const LightObject light, const vec3 direction, const vec3 normal, const vec2 texCoord)
{
	ray.Exit = true;
	if(ray.FrontFace)
	{
		ray.Attenuation = vec3(1.0);
		ray.EmitColor = vec4(m.Diffuse.rgb, 1.0);
	}
	else
	{
		ray.Attenuation = vec3(0);
		ray.EmitColor = vec4(0);
	}
}

void ScatterLambertian(inout RayPayload ray, const Material m, const LightObject light, const vec3 direction, const vec3 normal, const vec2 texCoord)
{
	ray.Attenuation = ray.Albedo.rgb;
	ray.ScatterDirection = AlignWithNormal( RandomInHemiSphere1(ray.RandomSeed), normal);
	ray.pdf = 1.0;
	ray.EmitColor = vec4(0);

	// Global Sun Check
	if(Camera.HasSun && ray.BounceCount <= 2)
	{
		const vec3 hitpos = ray.HitPos;
		const vec3 lightVector = AlignWithNormal( RandomInCone(ray.RandomSeed, cos_0_5degree), Camera.SunDirection.xyz);
		if(RandomFloat(ray.RandomSeed) < 0.33) {
			rayQueryEXT rayQuery;
			rayQueryInitializeEXT(rayQuery, Scene, gl_RayFlagsNoneEXT, 0xFF, hitpos, EPS, lightVector, INF);
			rayQueryProceedEXT(rayQuery);
			if (rayQueryGetIntersectionTypeEXT(rayQuery, true) == gl_RayQueryCommittedIntersectionNoneEXT  ) {
				// sun
				float ndotl = clamp(dot(lightVector, normal), 0, 1);
				ray.Exit = true;
				ray.EmitColor = vec4(Camera.SunColor.xyz * ray.Albedo.rgb, 1.0) * ndotl;
			}
			return;
		}
	}
	
	if( light.normal_area.w > 0 && RandomFloat(ray.RandomSeed) < 0.5 )
	{
		// scatter to light
		vec3 lightpos = light.p0.xyz + (light.p1.xyz - light.p0.xyz) * RandomFloat(ray.RandomSeed) + (light.p3.xyz - light.p0.xyz) *  RandomFloat(ray.RandomSeed);
		vec3 worldPos = ray.HitPos;
		vec3 tolight = lightpos - worldPos;

		float ndotl = dot(tolight, normal);
		
		if(ndotl >= 0)
		{
			float dist = length(tolight);
			tolight /= dist;

			const float epsVariance = .01;
			float cosine = max(dot(light.normal_area.xyz, -tolight), epsVariance);
			float light_pdf = dist * dist / (cosine * light.normal_area.w);

			ray.ScatterDirection = tolight;
			ray.pdf = M_1_PI / light_pdf;
		}
	}
}

void ScatterDieletricOpaque(inout RayPayload ray, const Material m, const LightObject light, const vec3 direction, const vec3 normal, const vec2 texCoord)
{
	ray.Attenuation = vec3(1.0);
	ray.ScatterDirection = ray.GBuffer.w > NEARzero ? ggxSampling(ray.RandomSeed, ray.GBuffer.w, reflect(direction, normal)) : reflect(direction, normal);
	ray.pdf = 1.0;
	ray.EmitColor = vec4(0);
}

void ScatterMetallic(inout RayPayload ray, const Material m, const LightObject light, const vec3 direction, const vec3 normal, const vec2 texCoord)
{
	ray.Attenuation = ray.Albedo.rgb;
	ray.ScatterDirection = ray.GBuffer.w < NEARzero ? reflect(direction, normal) : ( ray.GBuffer.w > 0.95 ? AlignWithNormal( RandomInHemiSphere1(ray.RandomSeed), reflect(direction, normal)) : ggxSampling(ray.RandomSeed, ray.GBuffer.w, reflect(direction, normal)));
	ray.pdf = 1.0;
	ray.EmitColor = vec4(0);
}

void ScatterDieletric(inout RayPayload ray, const Material m, const LightObject light, const vec3 direction, const vec3 normal, const vec2 texCoord)
{
	const vec3 outwardNormal = ray.FrontFace ? normal : -normal;
	const float niOverNt = ray.FrontFace ? 1 / m.RefractionIndex2 : m.RefractionIndex2;

	const vec3 refracted = refract(direction, outwardNormal, niOverNt);
	bool isReflection = sum_is_not_empty_abs(refracted) 
	? (
		RandomFloat(ray.RandomSeed) < Schlick( ray.FrontFace ? -dot(direction, normal) : m.RefractionIndex * dot(direction, normal), m.RefractionIndex)
		) 
	: true;
	
	if( isReflection )
	{
		// reflect
		const vec3 reflected = reflect(direction, outwardNormal);
		ray.Attenuation = vec3(1.0);

		ray.ScatterDirection = ray.GBuffer.w > NEARzero ? ggxSampling(ray.RandomSeed, ray.GBuffer.w, reflected) : reflected;
	}
	else
	{
		// refract
		ray.Attenuation = ray.Albedo.rgb;
		ray.ScatterDirection = refracted;
		ray.BounceCount--;
		ray.HitRefract = true;
	}
	
	ray.pdf = 1.0;
	ray.EmitColor = vec4(0);
}

// Mixture
void ScatterMixture(inout RayPayload ray, const Material m, const LightObject light, const vec3 direction, const vec3 normal, const vec2 texCoord)
{
	const float reflectProb = Schlick(ray.FrontFace ? -dot(direction, normal) : m.RefractionIndex * dot(direction, normal), m.RefractionIndex);
	
	if( RandomFloat(ray.RandomSeed) < ray.Metalness)
	{
		ScatterMetallic(ray,m,light,direction,normal,texCoord);
	}
	else if( RandomFloat(ray.RandomSeed) < reflectProb )
	{
		ScatterDieletricOpaque(ray,m,light,direction,normal,texCoord);
	}
	else
	{
		ScatterLambertian(ray,m,light,direction,normal,texCoord);
	}
}

void Scatter(inout RayPayload ray, const Material m, const LightObject light, const vec3 direction, vec3 normal, const vec2 texCoord, const float t, uint MaterialIndex)
{
	const vec4 texColor = m.DiffuseTextureId >= 0 ? srgbToLinear(texture(TextureSamplers[nonuniformEXT(m.DiffuseTextureId)], texCoord)) : vec4(1);
	const vec4 mra = m.MRATextureId >= 0 ? texture(TextureSamplers[nonuniformEXT(m.MRATextureId)], texCoord) : vec4(1);
	
	ray.Distance = t;
	ray.GBuffer = vec4(normal, m.Fuzziness * mra.g);
	ray.GBuffer.w = sqrt(ray.GBuffer.w);
	ray.Metalness = m.Metalness * mra.b;
	ray.Albedo = texColor * m.Diffuse;
	ray.FrontFace = dot(direction, normal) < 0;
	ray.MaterialIndex = MaterialIndex;
	ray.HitRefract = false;

	switch (m.MaterialModel)
	{
	case MaterialLambertian:
		ScatterLambertian(ray, m, light, direction, normal, texCoord);
		break;
	case MaterialMetallic:
		ScatterMetallic(ray, m, light, direction, normal, texCoord);
		break;
	case MaterialDielectric:
		ScatterDieletric(ray, m, light, direction, normal, texCoord);
		break;
	case MaterialDiffuseLight:
		ScatterDiffuseLight(ray, m, light, direction, normal, texCoord);
		break;
	case MaterialMixture:
		ScatterMixture(ray, m, light, direction, normal, texCoord);
	    break;
	case MaterialIsotropic:
	    ScatterLambertian(ray, m, light, direction, normal, texCoord);
	    break;
	}
}
#endif