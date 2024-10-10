#ifndef Const_Func

#define to_world(v, t, b, n) (mat3((t), (b), (n)) * (v))
#define to_local(v, T, B, N) ( (v) * mat3((T), (B), (N)) )

#define M_PI			3.14159265358979323846	// pi
#define M_TWO_PI		6.283185307179586476925	// 2*pi
#define M_PI_2			1.57079632679489661923	// pi/2
#define M_PI_4			0.785398163397448309616	// pi/4
#define M_1_PI			0.318309886183790671538	// 1/pi
#define M_2_PI			0.636619772367581343076	// 2/pi
#define M_1_OVER_TWO_PI	0.159154943091895335768	// 1/ (2*pi)

#define INF 1e32f
#define EPS 1e-3f
#define EPS2 2e-3f

#define NEARzero 1e-35f
#define isZERO(x) ((x)>-NEARzero && (x)<NEARzero)
#define allLessThanZERO(a)	( (a).x + (a).y + (a).z < NEARzero )
#define allGreaterZERO(a)	( (a).x + (a).y + (a).z >= NEARzero )
#define sum_is_empty_abs(a) (abs((a).x) + abs((a).y) + abs((a).z) < NEARzero)
#define sum_is_not_empty_abs(a) (abs((a).x) + abs((a).y) + abs((a).z) >= NEARzero)

#define Mix(a, b, c, bary) ( (a) + ((b) - (a)) * (bary).y + ((c) - (a)) * (bary).z )
#define luminance(rgb) (dot((rgb), vec3(0.212671F, 0.715160F, 0.072169F)))

#define saturate(x) ( clamp((x), 0.0F, 1.0F) )

float pow5(float x) { return x*x*x*x*x; }
float pow4(float x) { return x*x*x*x; }

// Polynomial approximation by Christophe Schlick
float Schlick(const float cosine, const float refractionIndex)
{
	float r0 = (1 - refractionIndex) / (1 + refractionIndex);
	r0 *= r0;
	//return r0 + (1 - r0) * pow5(1 - cosine);

	// taken from https://www.photometric.io/blog/improving-schlicks-approximation/
	return r0 + (1 - cosine - r0) * pow4(1 - cosine);
}

//onb revisited by Disney
void Onb(vec3 n, out vec3 b1, out vec3 b2) {
    float signZ = n.z < 0.f ? -1.f : 1.f;
    float a = -1.0f / (signZ + n.z);
    b2 = vec3(n.x * n.y * a, signZ + n.y * n.y * a, -n.y);
    b1 = vec3(1.0f + signZ * n.x * n.x * a, signZ * b2.x, -signZ * n.x);
}

//from Apple Metal Raytracing demo
void ONBAlignWithNormal(vec3 up, out vec3 right, out vec3 forward)
{
    right = normalize(cross(up, vec3(0.0072f, 1.0f, 0.0034f)));
    forward = cross(right, up);
}

//from nvpro_core
#define OUT_TYPE(T) out T
#define inline
//-----------------------------------------------------------------------------
// Builds an orthonormal basis: given only a normal vector, returns a
// tangent and bitangent.
//
// This uses the technique from "Improved accuracy when building an orthonormal
// basis" by Nelson Max, https://jcgt.org/published/0006/01/02.
// Any tangent-generating algorithm must produce at least one discontinuity
// when operating on a sphere (due to the hairy ball theorem); this has a
// small ring-shaped discontinuity at normal.z == -0.99998796.
//-----------------------------------------------------------------------------
inline void orthonormalBasis(vec3 normal, OUT_TYPE(vec3) tangent, OUT_TYPE(vec3) bitangent)
{
  if(normal.z < -0.99998796F)  // Handle the singularity
  {
    tangent   = vec3(0.0F, -1.0F, 0.0F);
    bitangent = vec3(-1.0F, 0.0F, 0.0F);
    return;
  }
  float a   = 1.0F / (1.0F + normal.z);
  float b   = -normal.x * normal.y * a;
  tangent   = vec3(1.0F - normal.x * normal.x * a, b, -normal.x);
  bitangent = vec3(b, 1.0f - normal.y * normal.y * a, -normal.y);
}

#define ONB Onb
//#define ONB ONBAlignWithNormal
//#define ONB orthonormalBasis

vec3 AlignWithNormal(vec3 ray, vec3 normal)
{
    vec3 T, B;
    ONB(normal, T, B);
    return to_world(ray, T, B, normal);
}
#define Const_Func
#endif