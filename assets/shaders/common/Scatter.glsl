#extension GL_EXT_nonuniform_qualifier : require

#include "Random.glsl"
#include "RayPayload.glsl"
#include "common/Const_Func.glsl" //pi consts, Schlick
#include "common/ColorFunc.glsl"

#ifndef scatter_inc
#define scatter_inc

#define sample_vndf

#ifdef sample_vndf
#define saturate(x) ( clamp((x), 0.0F, 1.0F) )

// Kenta Eto and Yusuke Tokuyoshi. 2023. Bounded VNDF Sampling for Smith-GGX Reflections.
// In SIGGRAPH Asia 2023 Technical Communications (SA Technical Communications '23), December 12-15, 2023, Sydney, NSW, Australia. ACM, New York, NY, USA, 4 pages.
// https://doi.org/10.1145/3610543.3626163
vec3 ggx_sample_vndf(vec2 alpha, vec3 wi_, vec2 uv) {
  vec3 wi = normalize(vec3(wi_.xy * alpha, wi_.z));
  // Sample a spherical cap
  //float k = (1.0f - a2) * s2 / (s2 + a2 * wi_.z * wi_.z); // Eq. 5

  float b = wi.z;
  if(wi_.z > 0.f) {
	  float a = saturate(min(alpha.x, alpha.y)); // Eq. 6
	  float s = 1.0f + length(wi_.xy); // Omit sgn for a <=1
	  b *= ((1.0f - a * a) * s * s / (s * s + a * a * wi_.z * wi_.z));
  }

  float z = (1.0f - uv.y) * (1.0f + b) -b;
  float cosPhi    = uv.x + uv.x - 1.0;
  vec3 o_std = vec3(sqrt(saturate(1.0f - z * z)) * vec2(cosPhi, sqrt(1.0 - cosPhi * cosPhi)), z);
  // Compute the microfacet normal m
  vec3 m_std = wi + o_std;
  return normalize(vec3(m_std.xy * alpha, m_std.z));
}

void ONBAlignWithNormal(vec3 up, out vec3 right, out vec3 forward)
{
    right = normalize(cross(up, vec3(0.0072f, 1.0f, 0.0034f)));
    forward = cross(right, up);
}

vec3 ggxSampling(inout uvec4 RandomSeed, float roughness, vec3 normal)
{
  vec3 tangent, bitangent;
  ONBAlignWithNormal(normal, tangent, bitangent);
  vec3 wm_ = ggx_sample_vndf(
                            vec2(roughness),
                            to_local(normal, tangent, bitangent, normal),
                            RandomFloat2(RandomSeed)
							);
  return to_world(wm_, tangent, bitangent, normal);
}
#else
vec3 ggxSampling(inout uvec4 RandomSeed, float roughness, vec3 normal)
{
  return AlignWithNormal( RandomInCone(RandomSeed, cos(roughness * M_PI_4)), normal );
}
#endif


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
	if(Camera.HasSun)
	{
		const vec3 hitpos = ray.HitPos;
		const vec3 lightVector = AlignWithNormal( RandomInCone(ray.RandomSeed, cos(0.5f / 180.f * 3.14159f)), Camera.SunDirection.xyz);
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
	ray.ScatterDirection = ray.GBuffer.w > NEARzero ? ggxSampling(ray.RandomSeed, ray.GBuffer.w, reflect(direction, normal)) : reflect(direction, normal);
	ray.pdf = 1.0;
	ray.EmitColor = vec4(0);
}

void ScatterDieletric(inout RayPayload ray, const Material m, const LightObject light, const vec3 direction, const vec3 normal, const vec2 texCoord)
{
	const vec3 outwardNormal = ray.FrontFace ? normal : -normal;
	const float niOverNt = ray.FrontFace ? 1 / m.RefractionIndex : m.RefractionIndex;

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
	}
	
	ray.pdf = 1.0;
	ray.EmitColor = vec4(0);
}

// Mixture
void ScatterMixture(inout RayPayload ray, const Material m, const LightObject light, const vec3 direction, const vec3 normal, const vec2 texCoord)
{
	const float reflectProb = Schlick(ray.FrontFace ? -dot(direction, normal) : m.RefractionIndex * dot(direction, normal), m.RefractionIndex);
	
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

void Scatter(inout RayPayload ray, const Material m, const LightObject light, const vec3 direction, const vec3 normal, const vec2 texCoord, const float t, uint MaterialIndex)
{
	const vec4 texColor = m.DiffuseTextureId >= 0 ? srgbToLinear(texture(TextureSamplers[nonuniformEXT(m.DiffuseTextureId)], texCoord)) : vec4(1);
	const vec4 mra = m.MRATextureId >= 0 ? texture(TextureSamplers[nonuniformEXT(m.MRATextureId)], texCoord) : vec4(1);

	ray.Distance = t;
	ray.GBuffer = vec4(normal, m.Fuzziness * mra.g);
	ray.Albedo = texColor * m.Diffuse;
	ray.FrontFace = dot(direction, normal) < 0;
	ray.MaterialIndex = MaterialIndex;

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