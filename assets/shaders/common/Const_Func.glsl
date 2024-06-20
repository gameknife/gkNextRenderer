#ifndef Const_Func

#define M_PI			3.14159265358979323846	// pi
#define M_TWO_PI		6.283185307179586476925	// 2*pi
#define M_PI_2			1.57079632679489661923	// pi/2
#define M_PI_4			0.785398163397448309616	// pi/4
#define M_1_PI			0.318309886183790671538	// 1/pi
#define M_2_PI			0.636619772367581343076	// 2/pi
#define M_1_OVER_TWO_PI	0.159154943091895335768	// 1/ (2*pi)

#define NEARzero 1e-35f
#define isZERO(x) ((x)>-NEARzero && (x)<NEARzero)
#define allLessThanZERO(a)	( (a).x + (a).y + (a).z < NEARzero )
#define allGreaterZERO(a)	( (a).x + (a).y + (a).z >= NEARzero )
#define sum_is_empty_abs(a) (abs((a).x) + abs((a).y) + abs((a).z) < NEARzero)
#define sum_is_not_empty_abs(a) (abs((a).x) + abs((a).y) + abs((a).z) >= NEARzero)

vec2 Mix(vec2 a, vec2 b, vec2 c, vec3 barycentrics)
{
	return a * barycentrics.x + b * barycentrics.y + c * barycentrics.z;
}

vec3 Mix(vec3 a, vec3 b, vec3 c, vec3 barycentrics)
{
	return a * barycentrics.x + b * barycentrics.y + c * barycentrics.z;
}

float pow5(float x) { return x*x*x*x*x; }

// Polynomial approximation by Christophe Schlick
float Schlick(const float cosine, const float refractionIndex)
{
	float r0 = (1 - refractionIndex) / (1 + refractionIndex);
	r0 *= r0;
	return r0 + (1 - r0) * pow5(1 - cosine);
}

// hsv to rgb
vec3 hsv2rgb(vec3 c) {
    vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

vec3 LinearToST2084UE(vec3 lin)
{
	const float m1 = 0.1593017578125; // = 2610. / 4096. * .25;
	const float m2 = 78.84375; // = 2523. / 4096. *  128;
	const float c1 = 0.8359375; // = 2392. / 4096. * 32 - 2413./4096.*32 + 1;
	const float c2 = 18.8515625; // = 2413. / 4096. * 32;
	const float c3 = 18.6875; // = 2392. / 4096. * 32;
	const float C = 10000.;

	vec3 L = lin/C;
	vec3 Lm = pow(L, vec3(m1));
	vec3 N1 = ( c1 + c2 * Lm );
	vec3 N2 = ( 1.0 + c3 * Lm );
	vec3 N = N1 * (1.0 / N2);
	vec3 P = pow( N, vec3(m2) );

	return P;
}

#define Const_Func
#endif