#ifndef UniformBufferObject_hlsl
#define UniformBufferObject_hlsl

#ifdef __cplusplus
#define glbool uint32_t
#define ALIGN_16 alignas(16)
#else
#define glbool bool
#define ALIGN_16
#endif

static const int SHADOWMAP_SIZE = 4096;
static const int CUBE_SIZE_XY = 192;//256;
static const int CUBE_SIZE_Z = 48;
static const float CUBE_UNIT = 0.25f;
static const float3 CUBE_OFFSET = float3(-CUBE_SIZE_XY / 2, -1.375f, -CUBE_SIZE_XY / 2) * CUBE_UNIT;

static const float CUBE_UNIT_FAR = 4.0f; // cover 0.8km x 0.8km x 0.16km
static const float3 CUBE_OFFSET_FAR = float3(-CUBE_SIZE_XY / 2, -1.375f, -CUBE_SIZE_XY / 2) * CUBE_UNIT_FAR;

#endif // UniformBufferObject_hlsl