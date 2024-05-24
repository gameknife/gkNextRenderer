#extension GL_EXT_nonuniform_qualifier : require

#include "Random.glsl"
#include "RayPayload.glsl"
//
//// Polynomial approximation by Christophe Schlick
//float Schlick(const float cosine, const float refractionIndex)
//{
//	float r0 = (1 - refractionIndex) / (1 + refractionIndex);
//	r0 *= r0;
//	return r0 + (1 - r0) * pow(1 - cosine, 5);
//}
//
//// Mixture
//RayPayload ScatterMixture(const Material m, const vec3 direction, const vec3 normal, const vec2 texCoord, const float t, inout uint seed, in uint bounce)
//{
//    const float dot = dot(direction, normal);
//	const vec3 outwardNormal = dot > 0 ? -normal : normal;
//	const float cosine = dot > 0 ? m.RefractionIndex * dot : -dot;
//	const float reflectProb = Schlick(cosine, m.RefractionIndex);
//	
//	const bool isScattered = dot < 0;
//	const vec4 texColor = m.DiffuseTextureId >= 0 ? texture(TextureSamplers[nonuniformEXT(m.DiffuseTextureId)], texCoord) : vec4(1);
//	const vec4 colorAndDistance = vec4(m.Diffuse.rgb * texColor.rgb * texColor.rgb, t);
//	const vec4 scatter = vec4( AlignWithNormal( RandomInHemiSphere(seed), outwardNormal), isScattered ? 1 : 0);
//	
//    return RandomFloat(seed) < reflectProb
//		? RayPayload( bounce == 0 ? mix(colorAndDistance, vec4(1.0,1.0,1.0,t), reflectProb) : vec4(1.0,1.0,1.0,t), 
//		vec4( AlignWithNormal( RandomInCone(seed, cos(m.Fuzziness * 45.f / 180.f * 3.14159f)), reflect(direction, outwardNormal)), isScattered ? 1 : 0), 
//		vec4(outwardNormal, m.Fuzziness),
//		seed,0,bounce)
//	: (
//		RandomFloat(seed) < m.Metalness ? RayPayload(colorAndDistance,
//		vec4( AlignWithNormal( RandomInCone(seed, cos(m.Fuzziness * 45.f / 180.f * 3.14159f)), reflect(direction, outwardNormal)), isScattered ? 1 : 0),
//		vec4(outwardNormal, m.Fuzziness),
//		seed,0,bounce) :
//		RayPayload(colorAndDistance, scatter, vec4(outwardNormal, m.Fuzziness), seed,0,bounce)
//	);
//}
//
//// Lambertian
//RayPayload ScatterLambertian(const Material m, const vec3 direction, const vec3 normal, const vec2 texCoord, const float t, inout uint seed, in uint bounce)
//{
//	const bool isScattered = dot(direction, normal) < 0;
//	const vec4 texColor = m.DiffuseTextureId >= 0 ? texture(TextureSamplers[nonuniformEXT(m.DiffuseTextureId)], texCoord) : vec4(1);
//	const vec4 colorAndDistance = vec4(m.Diffuse.rgb * texColor.rgb, t);
//	//const vec4 scatter = vec4(normal + RandomInUnitSphere(seed), isScattered ? 1 : 0);
//	const vec4 scatter = vec4( AlignWithNormal( RandomInHemiSphere(seed), normal), isScattered ? 1 : 0);
//	
//    return RayPayload(colorAndDistance, scatter, vec4(normal, m.Fuzziness), seed,0,bounce);
//}
//
//// Metallic
//RayPayload ScatterMetallic(const Material m, const vec3 direction, const vec3 normal, const vec2 texCoord, const float t, inout uint seed, in uint bounce)
//{
//	const vec3 reflected = reflect(direction, normal);
//	const bool isScattered = dot(reflected, normal) > 0;
//
//	const vec4 texColor = m.DiffuseTextureId >= 0 ? texture(TextureSamplers[nonuniformEXT(m.DiffuseTextureId)], texCoord) : vec4(1);
//	const vec4 colorAndDistance = vec4(m.Diffuse.rgb * texColor.rgb, t);
//	const vec4 scatter = vec4(AlignWithNormal( RandomInCone(seed, cos(m.Fuzziness * 45.f / 180.f * 3.14159f)), reflected), isScattered ? 1 : 0);
//
//	return RayPayload(colorAndDistance, scatter, vec4(normal, m.Fuzziness), seed,0,bounce);
//}
//
//// Dielectric
//RayPayload ScatterDieletric(const Material m, const vec3 direction, const vec3 normal, const vec2 texCoord, const float t, inout uint seed, in uint bounce)
//{
//    const vec3 reflected = reflect(direction, normal);
//	const float dot = dot(direction, normal);
//	const vec3 outwardNormal = dot > 0 ? -normal : normal;
//	const float niOverNt = dot > 0 ? m.RefractionIndex : 1 / m.RefractionIndex;
//	const float cosine = dot > 0 ? m.RefractionIndex * dot : -dot;
//
//	const vec3 refracted = refract(direction, outwardNormal, niOverNt);
//	const float reflectProb = refracted != vec3(0) ? Schlick(cosine, m.RefractionIndex) : 1;
//
//	const vec4 texColor = m.DiffuseTextureId >= 0 ? texture(TextureSamplers[nonuniformEXT(m.DiffuseTextureId)], texCoord) : vec4(1);
//	
//	const vec4 scatterRefl = vec4(AlignWithNormal( RandomInCone(seed, cos(m.Fuzziness * 45.f / 180.f * 3.14159f)), reflected), 1);
//	const vec4 scatterRefr = vec4(refracted, 1);
//	
//	return RandomFloat(seed) < reflectProb
//		? RayPayload(vec4(1,1,1,t), scatterRefl, vec4(normal, m.Fuzziness),seed,0,bounce)
//		: RayPayload(vec4(texColor.rgb * m.Diffuse.rgb, t), scatterRefr, vec4(normal, m.Fuzziness),seed,1,bounce);
//}
//
//// Isotropic
//RayPayload ScatterIsotropic(const Material m, const vec3 direction, const vec3 normal, const vec2 texCoord, const float t, inout uint seed, in uint bounce)
//{
//    const vec3 reflected = reflect(direction, normal);
//
//	const float dot = dot(direction, normal);
//	const vec3 outwardNormal = dot > 0 ? -normal : normal;
//	const float niOverNt = dot > 0 ? m.RefractionIndex : 1 / m.RefractionIndex;
//	const float cosine = dot > 0 ? m.RefractionIndex * dot : -dot;
//
//	const vec3 refracted = refract(direction, outwardNormal, niOverNt);
//	const float reflectProb = refracted != vec3(0) ? Schlick(cosine, m.RefractionIndex) : 1;
//
//	const vec4 texColor = m.DiffuseTextureId >= 0 ? texture(TextureSamplers[nonuniformEXT(m.DiffuseTextureId)], texCoord) : vec4(1);
//	
//	const vec4 scatterRefl = vec4(AlignWithNormal( RandomInCone(seed, cos(0.0f * 45.f / 180.f * 3.14159f)), reflected), 1);
//	const vec4 scatterRefr = vec4(AlignWithNormal( RandomInCone(seed, cos(m.Fuzziness * 45.f / 180.f * 3.14159f)), refracted), 1);
//	
//	return RandomFloat(seed) < reflectProb
//		? RayPayload(vec4(0.5,0.5,0.5,t), scatterRefl, vec4(normal, m.Fuzziness),seed,0,bounce)
//		: RayPayload(vec4(texColor.rgb * m.Diffuse.rgb * 1.25f, t), scatterRefr, vec4(normal, m.Fuzziness),seed,0,bounce);
//}
//
//// Diffuse Light
//RayPayload ScatterDiffuseLight(const Material m, const vec3 direction, const vec3 normal, const float t, inout uint seed, in uint bounce)
//{
//	const float dot = dot(direction, normal);
//	const vec4 colorAndDistance = vec4(m.Diffuse.rgb, -1);
//	//const vec4 scatter = vec4(1, 1, 1, dot > 0  ? 1 : 0); consider light normal
//	const vec4 scatter = vec4(1, 1, 1, 1);
//
//	return RayPayload(colorAndDistance, scatter, vec4(1,0,0,0), seed,0,bounce);
//}
//

void ScatterDiffuseLight(inout RayPayload ray, const Material m, const vec3 direction, const vec3 normal, const vec2 texCoord)
{
	ray.FrontFace = dot(direction, normal) < 0 ? 1 : 0;
    if(ray.FrontFace > 0)
    {
        ray.Attenuation = m.Diffuse.rgb;
        ray.EmitColor = vec4(m.Diffuse.rgb, 1.0);
        ray.Distance = -1;
    }
}

void ScatterLambertian(inout RayPayload ray, const Material m, const vec3 direction, const vec3 normal, const vec2 texCoord)
{
	ray.FrontFace = dot(direction, normal) < 0 ? 1 : 0;
	const vec4 texColor = m.DiffuseTextureId >= 0 ? texture(TextureSamplers[nonuniformEXT(m.DiffuseTextureId)], texCoord) : vec4(1);

	ray.Attenuation = m.Diffuse.rgb * texColor.rgb;
	ray.ScatterDirection = AlignWithNormal( RandomInHemiSphere1(ray.RandomSeed), normal);
	ray.pdf = 1.0;
	ray.EmitColor = vec4(0);
	ray.GBuffer = vec4(normal, m.Fuzziness);
	
	if( RandomFloat(ray.RandomSeed) < 0.5 )
	{
		// scatter to light
		vec3 lightpos = vec3( mix( 213, 343, RandomFloat(ray.RandomSeed)) ,555, mix( -213, -343, RandomFloat(ray.RandomSeed)) );
		vec3 worldPos = gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT;
		vec3 tolight = lightpos - worldPos;
		float dist = length(tolight);
		tolight = tolight / dist;

		ray.ScatterDirection = tolight;
		float light_pdf = dist * dist / (abs(ray.ScatterDirection.y) * 130 * 130);
		ray.pdf = 1.0f / light_pdf;
	}
}

void Scatter(inout RayPayload ray, const Material m, const vec3 direction, const vec3 normal, const vec2 texCoord, const float t)
{
    ray.Distance = t;

	switch (m.MaterialModel)
	{
	case MaterialLambertian:
		ScatterLambertian(ray, m, direction, normal, texCoord);
		break;
	case MaterialMetallic:
		ScatterLambertian(ray, m, direction, normal, texCoord);
		break;
	case MaterialDielectric:
		ScatterLambertian(ray, m, direction, normal, texCoord);
		break;
	case MaterialDiffuseLight:
		ScatterDiffuseLight(ray, m, direction, normal, texCoord);
		break;
	case MaterialMixture:
	    ScatterLambertian(ray, m, direction, normal, texCoord);
	    break;
	case MaterialIsotropic:
	    ScatterLambertian(ray, m, direction, normal, texCoord);
	    break;
	}
}

