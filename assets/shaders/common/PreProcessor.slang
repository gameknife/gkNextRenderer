#define to_local(v, t, b, n) (mul(float3x3((t), (b), (n)), (v)))
#define to_world(v, T, B, N) (mul((v), float3x3((T), (B), (N))))

#define M_PI            3.14159265358979323846  // pi
#define M_TWO_PI        6.283185307179586476925 // 2*pi
#define M_PI_2          1.57079632679489661923  // pi/2
#define M_PI_4          0.785398163397448309616 // pi/4
#define M_1_PI          0.318309886183790671538 // 1/pi
#define M_2_PI          0.636619772367581343076 // 2/pi
#define M_1_OVER_TWO_PI 0.159154943091895335768 // 1/(2*pi)

#define INF 1e32f
#define EPS 1e-3f
#define EPS2 2e-3f
#define EPS10 1e-2f

#define NEARzero 1e-35f
#define isZERO(x) ((x) > -NEARzero && (x) < NEARzero)
#define allLessThanZERO(a)  ((a).x + (a).y + (a).z < NEARzero)
#define allGreaterZERO(a)   ((a).x + (a).y + (a).z >= NEARzero)
#define sum_is_empty_abs(a) (abs((a).x) + abs((a).y) + abs((a).z) < NEARzero)
#define sum_is_not_empty_abs(a) (abs((a).x) + abs((a).y) + abs((a).z) >= NEARzero)

#define Mix(a, b, c, bary) ((a) + ((b) - (a)) * (bary).y + ((c) - (a)) * (bary).z)
#define luminance(rgb) (dot((rgb), float3(0.212671F, 0.715160F, 0.072169F)))

#define saturate(x) (clamp((x), 0.0F, 1.0F))
