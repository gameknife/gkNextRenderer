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

#define INF 1e32
#define EPS 1e-3

#define NEARzero 1e-35f
#define isZERO(x) ((x)>-NEARzero && (x)<NEARzero)
#define allLessThanZERO(a)	( (a).x + (a).y + (a).z < NEARzero )
#define allGreaterZERO(a)	( (a).x + (a).y + (a).z >= NEARzero )
#define sum_is_empty_abs(a) (abs((a).x) + abs((a).y) + abs((a).z) < NEARzero)
#define sum_is_not_empty_abs(a) (abs((a).x) + abs((a).y) + abs((a).z) >= NEARzero)

#define Mix(a, b, c, barycentrics) ( (a) * (barycentrics).x + (b) * (barycentrics).y + (c) * (barycentrics).z )
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

vec3 AlignWithNormal(vec3 ray, vec3 up)
{
    vec3 right = normalize(cross(up, vec3(0.0072f, 1.0f, 0.0034f)));
    vec3 forward = cross(right, up);
    return to_world(ray, right, up, forward);
}

void ONBAlignWithNormal(vec3 up, out vec3 right, out vec3 forward)
{
    right = normalize(cross(up, vec3(0.0072f, 1.0f, 0.0034f)));
    forward = cross(right, up);
}

#define Const_Func
#endif