#extension GL_EXT_nonuniform_qualifier : require

#include "Random.glsl"
#include "RayPayload.glsl"
#include "common/Const_Func.glsl" //pi consts, Schlick

void ScatterDiffuseLight(inout RayPayload ray, const Material m, const LightObject light, const vec3 direction, const vec3 normal, const vec2 texCoord)
{
	ray.Exit = 1;
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
	if(Camera.HasSun)
	{
		const vec3 hitpos = gl_WorldRayOriginEXT + gl_HitTEXT * gl_WorldRayDirectionEXT;
		const vec3 lightVector = AlignWithNormal( RandomInCone(ray.RandomSeed, cos(0.5f / 180.f * 3.14159f)), Camera.SunDirection.xyz);
		if(RandomFloat(ray.RandomSeed) < 0.33) {
			rayQueryEXT rayQuery;
			rayQueryInitializeEXT(rayQuery, Scene, gl_RayFlagsNoneEXT, 0xFF, hitpos, 0.01, lightVector, 10000.0);
			rayQueryProceedEXT(rayQuery);
			if (rayQueryGetIntersectionTypeEXT(rayQuery, true) == gl_RayQueryCommittedIntersectionNoneEXT  ) {
				// sun
				float ndotl = clamp(dot(lightVector, normal), 0, 1);
				ray.Exit = 1;
				ray.EmitColor = vec4(Camera.SunColor.xyz, 1.0) * ndotl;
			}
			return;
		}
	}
	
	if( light.normal_area.w > 0 && RandomFloat(ray.RandomSeed) < 0.5 )
	{
		// scatter to light
		vec3 lightpos = light.p0.xyz + (light.p1.xyz - light.p0.xyz) * RandomFloat(ray.RandomSeed) + (light.p3.xyz - light.p0.xyz) *  RandomFloat(ray.RandomSeed);
		vec3 worldPos = gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT;
		vec3 tolight = lightpos - worldPos;

		float ndotl = ray.FrontFace ? -dot(tolight, normal) : dot(tolight, normal);
		
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
	ray.ScatterDirection = AlignWithNormal( RandomInCone(ray.RandomSeed, cos(m.Fuzziness * 45.f / 180.f * 3.14159f)), reflect(direction, normal));
	ray.pdf = 1.0;
	ray.EmitColor = vec4(0);
}

void ScatterMetallic(inout RayPayload ray, const Material m, const LightObject light, const vec3 direction, const vec3 normal, const vec2 texCoord)
{
	ray.Attenuation = ray.Albedo.rgb;
	ray.ScatterDirection = AlignWithNormal( RandomInCone(ray.RandomSeed, cos(m.Fuzziness * 45.f / 180.f * 3.14159f)), reflect(direction, normal));
	ray.pdf = 1.0;
	ray.EmitColor = vec4(0);
}

void ScatterDieletric(inout RayPayload ray, const Material m, const LightObject light, const vec3 direction, const vec3 normal, const vec2 texCoord)
{
	const vec3 outwardNormal = ray.FrontFace ? normal : -normal;
	const float niOverNt = ray.FrontFace ? 1 / m.RefractionIndex : m.RefractionIndex;
	const float cosine = ray.FrontFace ? -dot(direction, normal) : m.RefractionIndex * dot(direction, normal);

	const vec3 refracted = refract(direction, outwardNormal, niOverNt);
	const float reflectProb = sum_is_not_empty_abs(refracted) ? Schlick(cosine, m.RefractionIndex) : 1;
	
	if( RandomFloat(ray.RandomSeed) < reflectProb )
	{
		// reflect
		const vec3 reflected = reflect(direction, outwardNormal);
		ray.Attenuation = vec3(1.0);
		ray.ScatterDirection = AlignWithNormal( RandomInCone(ray.RandomSeed, cos(m.Fuzziness * 45.f / 180.f * 3.14159f)), reflected);
	}
	else
	{
		// refract
		ray.Attenuation = ray.Albedo.rgb;
		ray.ScatterDirection = refracted;
		ray.BounceCount--;
	}
	
	ray.pdf = 1.0;
	ray.EmitColor = vec4(0);
}

// Mixture
void ScatterMixture(inout RayPayload ray, const Material m, const LightObject light, const vec3 direction, const vec3 normal, const vec2 texCoord)
{
	const float cosine = ray.FrontFace ? -dot(direction, normal) : m.RefractionIndex * dot(direction, normal);
	const float reflectProb = Schlick(cosine, m.RefractionIndex);
	
    if( RandomFloat(ray.RandomSeed) < reflectProb )
	{
		ScatterDieletricOpaque(ray,m,light,direction,normal,texCoord);
	}
	else if( RandomFloat(ray.RandomSeed) < m.Metalness)
	{
		ScatterMetallic(ray,m,light,direction,normal,texCoord);
	}
	else
	{
		ScatterLambertian(ray,m,light,direction,normal,texCoord);
	}
}

void Scatter(inout RayPayload ray, const Material m, const LightObject light, const vec3 direction, const vec3 normal, const vec2 texCoord, const float t)
{
	const vec4 texColor = m.DiffuseTextureId >= 0 ? texture(TextureSamplers[nonuniformEXT(m.DiffuseTextureId)], texCoord) : vec4(1);
	
    ray.Distance = t;
	ray.GBuffer = vec4(normal, m.Fuzziness);
	ray.Albedo = texColor * texColor * m.Diffuse;
	ray.FrontFace = dot(direction, normal) < 0;

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

