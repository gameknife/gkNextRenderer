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

// Mixture
RayPayload ScatterMixture(const Material m, const vec3 direction, const vec3 normal, const vec2 texCoord, const float t, inout uint seed)
{
    const float dot = dot(direction, normal);
	const vec3 outwardNormal = dot > 0 ? -normal : normal;
	const float cosine = dot > 0 ? m.RefractionIndex * dot : -dot;
	const float reflectProb = Schlick(cosine, m.RefractionIndex);
	
	const bool isScattered = dot < 0;
	const vec4 texColor = m.DiffuseTextureId >= 0 ? texture(TextureSamplers[nonuniformEXT(m.DiffuseTextureId)], texCoord) : vec4(1);
	const vec4 colorAndDistance = vec4(m.Diffuse.rgb * texColor.rgb * texColor.rgb, t);
	const vec4 scatter = vec4( AlignWithNormal( RandomInHemiSphere(seed), outwardNormal), isScattered ? 1 : 0);
	
    return RandomFloat(seed) < reflectProb
		? RayPayload(vec4(1.0,1.0,1.0,t), 
		vec4( AlignWithNormal( RandomInCone(seed, cos(m.Fuzziness * 45.f / 180.f * 3.14159f)), reflect(direction, outwardNormal)), isScattered ? 1 : 0), 
		vec4(outwardNormal, m.Fuzziness),
		seed,0)
	: (
			RandomFloat(seed) < m.Metalness ? RayPayload(colorAndDistance,
vec4( AlignWithNormal( RandomInCone(seed, cos(m.Fuzziness * 45.f / 180.f * 3.14159f)), reflect(direction, outwardNormal)), isScattered ? 1 : 0),
vec4(outwardNormal, m.Fuzziness),
seed,0) :
RayPayload(colorAndDistance, scatter, vec4(outwardNormal, m.Fuzziness), seed,0)
	);
}

// Lambertian
RayPayload ScatterLambertian(const Material m, const vec3 direction, const vec3 normal, const vec2 texCoord, const float t, inout uint seed)
{
	const bool isScattered = dot(direction, normal) < 0;
	const vec4 texColor = m.DiffuseTextureId >= 0 ? texture(TextureSamplers[nonuniformEXT(m.DiffuseTextureId)], texCoord) : vec4(1);
	const vec4 colorAndDistance = vec4(m.Diffuse.rgb * texColor.rgb, t);
	//const vec4 scatter = vec4(normal + RandomInUnitSphere(seed), isScattered ? 1 : 0);
	const vec4 scatter = vec4( AlignWithNormal( RandomInHemiSphere(seed), normal), isScattered ? 1 : 0);
	
    return RayPayload(colorAndDistance, scatter, vec4(normal, m.Fuzziness), seed,0);
}

// Metallic
RayPayload ScatterMetallic(const Material m, const vec3 direction, const vec3 normal, const vec2 texCoord, const float t, inout uint seed)
{
	const vec3 reflected = reflect(direction, normal);
	const bool isScattered = dot(reflected, normal) > 0;

	const vec4 texColor = m.DiffuseTextureId >= 0 ? texture(TextureSamplers[nonuniformEXT(m.DiffuseTextureId)], texCoord) : vec4(1);
	const vec4 colorAndDistance = vec4(m.Diffuse.rgb * texColor.rgb, t);
	const vec4 scatter = vec4(AlignWithNormal( RandomInCone(seed, cos(m.Fuzziness * 45.f / 180.f * 3.14159f)), reflected), isScattered ? 1 : 0);

	return RayPayload(colorAndDistance, scatter, vec4(normal, m.Fuzziness), seed,0);
}

// Dielectric
RayPayload ScatterDieletric(const Material m, const vec3 direction, const vec3 normal, const vec2 texCoord, const float t, inout uint seed)
{
    const vec3 reflected = reflect(direction, normal);
	const float dot = dot(direction, normal);
	const vec3 outwardNormal = dot > 0 ? -normal : normal;
	const float niOverNt = dot > 0 ? m.RefractionIndex : 1 / m.RefractionIndex;
	const float cosine = dot > 0 ? m.RefractionIndex * dot : -dot;

	const vec3 refracted = refract(direction, outwardNormal, niOverNt);
	const float reflectProb = refracted != vec3(0) ? Schlick(cosine, m.RefractionIndex) : 1;

	const vec4 texColor = m.DiffuseTextureId >= 0 ? texture(TextureSamplers[nonuniformEXT(m.DiffuseTextureId)], texCoord) : vec4(1);
	
	const vec4 scatterRefl = vec4(AlignWithNormal( RandomInCone(seed, cos(m.Fuzziness * 45.f / 180.f * 3.14159f)), reflected), 1);
	const vec4 scatterRefr = vec4(refracted, 1);
	
	return RandomFloat(seed) < reflectProb
		? RayPayload(vec4(1,1,1,t), scatterRefl, vec4(normal, m.Fuzziness),seed,0)
		: RayPayload(vec4(texColor.rgb * m.Diffuse.rgb, t), scatterRefr, vec4(normal, m.Fuzziness),seed,1);
}

// Isotropic
RayPayload ScatterIsotropic(const Material m, const vec3 direction, const vec3 normal, const vec2 texCoord, const float t, inout uint seed)
{
    const vec3 reflected = reflect(direction, normal);

	const float dot = dot(direction, normal);
	const vec3 outwardNormal = dot > 0 ? -normal : normal;
	const float niOverNt = dot > 0 ? m.RefractionIndex : 1 / m.RefractionIndex;
	const float cosine = dot > 0 ? m.RefractionIndex * dot : -dot;

	const vec3 refracted = refract(direction, outwardNormal, niOverNt);
	const float reflectProb = refracted != vec3(0) ? Schlick(cosine, m.RefractionIndex) : 1;

	const vec4 texColor = m.DiffuseTextureId >= 0 ? texture(TextureSamplers[nonuniformEXT(m.DiffuseTextureId)], texCoord) : vec4(1);
	
	const vec4 scatterRefl = vec4(AlignWithNormal( RandomInCone(seed, cos(0.0f * 45.f / 180.f * 3.14159f)), reflected), 1);
	const vec4 scatterRefr = vec4(AlignWithNormal( RandomInCone(seed, cos(m.Fuzziness * 45.f / 180.f * 3.14159f)), refracted), 1);
	
	return RandomFloat(seed) < reflectProb
		? RayPayload(vec4(0.5,0.5,0.5,t), scatterRefl, vec4(normal, m.Fuzziness),seed,0)
		: RayPayload(vec4(texColor.rgb * m.Diffuse.rgb * 1.25f, t), scatterRefr, vec4(normal, m.Fuzziness),seed,0);
}

// Diffuse Light
RayPayload ScatterDiffuseLight(const Material m, const vec3 direction, const vec3 normal, const float t, inout uint seed)
{
	const float dot = dot(direction, normal);
	const vec4 colorAndDistance = vec4(m.Diffuse.rgb, -1);//dot > 0  ? vec4(m.Diffuse.rgb, -1) : vec4(0,0,0, -1);
	const vec4 scatter = vec4(1, 0, 0, 0);

	return RayPayload(colorAndDistance, scatter, vec4(1,0,0,0), seed,0);
}

RayPayload Scatter(const Material m, const vec3 direction, const vec3 normal, const vec2 texCoord, const float t, inout uint seed)
{
	const vec3 normDirection = normalize(direction);

	switch (m.MaterialModel)
	{
	case MaterialLambertian:
		return ScatterLambertian(m, normDirection, normal, texCoord, t, seed);
	case MaterialMetallic:
		return ScatterMetallic(m, normDirection, normal, texCoord, t, seed);
	case MaterialDielectric:
		return ScatterDieletric(m, normDirection, normal, texCoord, t, seed);
	case MaterialDiffuseLight:
		return ScatterDiffuseLight(m, normDirection, normal, t, seed);
	case MaterialMixture:
	    return ScatterMixture(m, normDirection, normal, texCoord, t, seed);
	case MaterialIsotropic:
	    return ScatterIsotropic(m, normDirection, normal, texCoord, t, seed);
	}
}

