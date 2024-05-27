#extension GL_EXT_nonuniform_qualifier : require

#include "Random.glsl"
#include "RayPayload.glsl"


// Polynomial approximation by Christophe Schlick
float Schlick(const float cosine, const float refractionIndex)
{
	float r0 = (1 - refractionIndex) / (1 + refractionIndex);
	r0 *= r0;
	return r0 + (1 - r0) * pow(1 - cosine, 5);
}

void ScatterDiffuseLight(inout RayPayload ray, const Material m, const LightObject light, const vec3 direction, const vec3 normal, const vec2 texCoord)
{
	ray.FrontFace = dot(direction, normal) < 0 ? 1 : 0;
	ray.Distance = -1;
	if(ray.FrontFace > 0)
	{
		ray.Attenuation = m.Diffuse.rgb;
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
	ray.FrontFace = dot(direction, normal) < 0 ? 1 : 0;
	ray.Attenuation = ray.Albedo.rgb;
	ray.ScatterDirection = AlignWithNormal( RandomInHemiSphere1(ray.RandomSeed), normal);
	ray.pdf = 1.0;
	ray.EmitColor = vec4(0);

	if( light.area > 0 && RandomFloat(ray.RandomSeed) < 0.5 )
	{
		// scatter to light
		vec3 lightpos = mix(light.WorldPosMin.xyz, light.WorldPosMax.xyz, vec3(RandomFloat(ray.RandomSeed), RandomFloat(ray.RandomSeed), RandomFloat(ray.RandomSeed)));
		vec3 worldPos = gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT;
		vec3 tolight = lightpos - worldPos;
		float dist = length(tolight);
		tolight = tolight / dist;
		
		float cosine = dot(light.WorldDirection.xyz, -tolight);
		float light_pdf = dist * dist / (cosine * light.area);
		float ndotl = dot(tolight, normal);
		
		if(ndotl > 0)
		{
			ray.ScatterDirection = tolight;
			ray.pdf = 1.0f / light_pdf;
		}
	}
}

void ScatterDieletricOpaque(inout RayPayload ray, const Material m, const LightObject light, const vec3 direction, const vec3 normal, const vec2 texCoord)
{
	ray.FrontFace = dot(direction, normal) < 0 ? 1 : 0;
	ray.Attenuation = vec3(1.0);
	ray.ScatterDirection = AlignWithNormal( RandomInCone(ray.RandomSeed, cos(m.Fuzziness * 45.f / 180.f * 3.14159f)), reflect(direction, normal));
	ray.pdf = 1.0;
	ray.EmitColor = vec4(0);
}

void ScatterMetallic(inout RayPayload ray, const Material m, const LightObject light, const vec3 direction, const vec3 normal, const vec2 texCoord)
{
	ray.FrontFace = dot(direction, normal) < 0 ? 1 : 0;
	ray.Attenuation = ray.Albedo.rgb;
	ray.ScatterDirection = AlignWithNormal( RandomInCone(ray.RandomSeed, cos(m.Fuzziness * 45.f / 180.f * 3.14159f)), reflect(direction, normal));
	ray.pdf = 1.0;
	ray.EmitColor = vec4(0);
}

void ScatterDieletric(inout RayPayload ray, const Material m, const LightObject light, const vec3 direction, const vec3 normal, const vec2 texCoord)
{
	const float dot = dot(direction, normal);
	ray.FrontFace = dot < 0 ? 1 : 0;

	const vec3 outwardNormal = dot > 0 ? -normal : normal;
	const float niOverNt = dot > 0 ? m.RefractionIndex : 1 / m.RefractionIndex;
	const float cosine = dot > 0 ? m.RefractionIndex * dot : -dot;

	const vec3 refracted = refract(direction, outwardNormal, niOverNt);
	const float reflectProb = refracted != vec3(0) ? Schlick(cosine, m.RefractionIndex) : 1;

	const vec3 reflected = reflect(direction, outwardNormal);
	
	if( RandomFloat(ray.RandomSeed) < reflectProb )
	{
		// reflect
		ray.Attenuation = vec3(1.0);
		ray.ScatterDirection = AlignWithNormal( RandomInCone(ray.RandomSeed, cos(m.Fuzziness * 45.f / 180.f * 3.14159f)), reflected);
	}
	else
	{
		// refract
		ray.Attenuation = ray.Albedo.rgb;
		ray.ScatterDirection = refracted;
	}
	
	ray.pdf = 1.0;
	ray.EmitColor = vec4(0);
	ray.GBuffer = vec4(normal, m.Fuzziness);
}

// Mixture
void ScatterMixture(inout RayPayload ray, const Material m, const LightObject light, const vec3 direction, const vec3 normal, const vec2 texCoord)
{
    const float dot = dot(direction, normal);
	const float cosine = dot > 0 ? m.RefractionIndex * dot : -dot;
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

