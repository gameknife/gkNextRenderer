/*
The MIT License (MIT)

Copyright (c) 2024, Jacco Bikker / Breda University of Applied Sciences.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

// 2024
// Nov 25: version 0.9.8 : FATAL_ERROR_IF interface.
// Nov 22: version 0.9.7 : Bug fix release.
// Nov 21: version 0.9.6 : Interface for IsOccluded and batches.
// Nov 20: version 0.9.5 : CWBVH traversal on GPU.
// Nov 19: version 0.9.2 : 4-way traversal on GPU.
// Nov 18: version 0.9.1 : Added custom alloc/free (tcantenot).
// Mov 16: version 0.9.0 : (external) OpenCL in speedtest.
// Nov 15: version 0.8.3 : Incremental update / bugfixes.
// Nov 14: version 0.8.0 : ARM/NEON support.
// Nov 13: version 0.7.5 : Support for WASM with EMSCRIPTEN. 
// Nov 12: version 0.7.0 : CWBVH construction and traversal.
// Nov 11: version 0.5.1 : SBVH builder, BVH4_GPU traversal.
// Nov 10: version 0.4.2 : BVH4/8, gpu-friendly BVH4.
// Nov 09: version 0.4.0 : Layouts, BVH optimizer.
// Nov 08: version 0.3.0 : BuildAVX in g++.
// Oct 30: version 0.1.0 : Initial release.
// Oct 29: version 0.0.1 : Establishing interface.
//

//
// Use this in *one* .c or .cpp
//   #define TINYBVH_IMPLEMENTATION
//   #include "tiny_bvh.h"
//

// How to use:
// See tiny_bvh_test.cpp for basic usage. In short:
// instantiate a BVH: tinybvh::BVH bvh;
// build it: bvh.Build( (tinybvh::bvhvec4*)triangleData, TRIANGLE_COUNT );
// ..where triangleData is an array of four-component float vectors:
// - For a single triangle, provide 3 vertices,
// - For each vertex provide x, y and z.
// The fourth float in each vertex is a dummy value and exists purely for
// a more efficient layout of the data in memory.

// More information about the BVH data structure:
// https://jacco.ompf2.com/2022/04/13/how-to-build-a-bvh-part-1-basics

// Further references:
// - Parallel Spatial Splits in Bounding Volume Hierarchies:
//   https://diglib.eg.org/items/f55715b1-9e56-4b40-af73-59d3dfba9fe7
// - Heuristics for ray tracing using space subdivision:
//   https://graphicsinterface.org/wp-content/uploads/gi1989-22.pdf
// - Heuristic Ray Shooting Algorithms:
//   https://dcgi.fel.cvut.cz/home/havran/DISSVH/phdthesis.html

// Author and contributors:
// Jacco Bikker: BVH code and examples
// Eddy L O Jansson: g++ / clang support
// Aras Pranckevičius: non-Intel architecture support
// Jefferson Amstutz: CMake surpport
// Christian Oliveros: WASM / EMSCRIPTEN support
// Thierry Cantenot: user-defined alloc & free

#ifndef TINY_BVH_H_
#define TINY_BVH_H_

// binned BVH building: bin count
#define BVHBINS 8

// SAH BVH building: Heuristic parameters
// CPU builds: C_INT = 1, C_TRAV = 0.01 seems optimal.
#define C_INT	1
#define C_TRAV	0.01f

// include fast AVX BVH builder
#if !defined(__APPLE__) && (defined(__x86_64__) || defined(_M_X64) || defined(__wasm_simd128__) || defined(__wasm_relaxed_simd__))
#define BVH_USEAVX
#include "immintrin.h" // for __m128 and __m256
#elif defined(__aarch64__) || defined(_M_ARM64)
#define BVH_USENEON
#include "arm_neon.h"
#endif

// library version
#define TINY_BVH_VERSION_MAJOR	0
#define TINY_BVH_VERSION_MINOR	9
#define TINY_BVH_VERSION_SUB	8

// ============================================================================
//
//        P R E L I M I N A R I E S
// 
// ============================================================================

// needful includes
#ifdef _MSC_VER // Visual Studio / C11
#include <malloc.h> // for alloc/free
#include <stdio.h> // for fprintf
#include <math.h> // for sqrtf, fabs
#include <string.h> // for memset
#include <stdlib.h> // for exit(1)
#else // Emscripten / gcc / clang
#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <cstring>
#endif

// aligned memory allocation
// note: formally size needs to be a multiple of 'alignment'. See:
// https://en.cppreference.com/w/c/memory/aligned_alloc
// EMSCRIPTEN enforces this.
// Copy of the same construct in tinyocl, different namespace.
namespace tinybvh {
inline size_t make_multiple_64( size_t x ) { return (x + 63) & ~0x3f; }
}
#ifdef _MSC_VER // Visual Studio / C11
#define ALIGNED( x ) __declspec( align( x ) )
namespace tinybvh {
inline void* malloc64( size_t size, void* = nullptr )
{
	return size == 0 ? 0 : _aligned_malloc( make_multiple_64( size ), 64 );
}
inline void free64( void* ptr, void* = nullptr ) { _aligned_free( ptr ); }
}
#else // EMSCRIPTEN / gcc / clang
#define ALIGNED( x ) __attribute__( ( aligned( x ) ) )
#if defined(__x86_64__) || defined(_M_X64) || defined(__wasm_simd128__) || defined(__wasm_relaxed_simd__)
#include <xmmintrin.h>
namespace tinybvh {
inline void* malloc64( size_t size, void* = nullptr )
{
	return size == 0 ? 0 : _mm_malloc( make_multiple_64( size ), 64 );
}
inline void free64( void* ptr, void* = nullptr ) { _mm_free( ptr ); }
}
#else
namespace tinybvh {
inline void* malloc64( size_t size, void* = nullptr )
{
	return size == 0 ? 0 : aligned_alloc( 64, make_multiple_64( size ) );
}
inline void free64( void* ptr, void* = nullptr ) { free( ptr ); }
}
#endif
#endif

namespace tinybvh {

#ifdef _MSC_VER
// Suppress a warning caused by the union of x,y,.. and cell[..] in vectors.
// We need this union to address vector components either by name or by index.
// The warning is re-enabled right after the definition of the data types.
#pragma warning ( push )
#pragma warning ( disable: 4201 /* nameless struct / union */ )
#endif

struct bvhvec3;
struct ALIGNED( 16 ) bvhvec4
{
	// vector naming is designed to not cause any name clashes.
	bvhvec4() = default;
	bvhvec4( const float a, const float b, const float c, const float d ) : x( a ), y( b ), z( c ), w( d ) {}
	bvhvec4( const float a ) : x( a ), y( a ), z( a ), w( a ) {}
	bvhvec4( const bvhvec3 & a );
	bvhvec4( const bvhvec3 & a, float b );
	float& operator [] ( const int i ) { return cell[i]; }
	union { struct { float x, y, z, w; }; float cell[4]; };
};

struct ALIGNED( 8 ) bvhvec2
{
	bvhvec2() = default;
	bvhvec2( const float a, const float b ) : x( a ), y( b ) {}
	bvhvec2( const float a ) : x( a ), y( a ) {}
	bvhvec2( const bvhvec4 a ) : x( a.x ), y( a.y ) {}
	float& operator [] ( const int i ) { return cell[i]; }
	union { struct { float x, y; }; float cell[2]; };
};

struct bvhvec3
{
	bvhvec3() = default;
	bvhvec3( const float a, const float b, const float c ) : x( a ), y( b ), z( c ) {}
	bvhvec3( const float a ) : x( a ), y( a ), z( a ) {}
	bvhvec3( const bvhvec4 a ) : x( a.x ), y( a.y ), z( a.z ) {}
	float halfArea() { return x < -1e30f ? 0 : (x * y + y * z + z * x); } // for SAH calculations
	float& operator [] ( const int i ) { return cell[i]; }
	union { struct { float x, y, z; }; float cell[3]; };
};
struct bvhint3
{
	bvhint3() = default;
	bvhint3( const int a, const int b, const int c ) : x( a ), y( b ), z( c ) {}
	bvhint3( const int a ) : x( a ), y( a ), z( a ) {}
	bvhint3( const bvhvec3& a ) { x = (int)a.x, y = (int)a.y, z = (int)a.z; }
	int& operator [] ( const int i ) { return cell[i]; }
	union { struct { int x, y, z; }; int cell[3]; };
};
struct bvhint2
{
	bvhint2() = default;
	bvhint2( const int a, const int b ) : x( a ), y( b ) {}
	bvhint2( const int a ) : x( a ), y( a ) {}
	int x, y;
};
struct bvhuint2
{
	bvhuint2() = default;
	bvhuint2( const unsigned a, const unsigned b ) : x( a ), y( b ) {}
	bvhuint2( const unsigned a ) : x( a ), y( a ) {}
	unsigned x, y;
};

#ifdef TINYBVH_IMPLEMENTATION
bvhvec4::bvhvec4( const bvhvec3& a ) { x = a.x; y = a.y; z = a.z; w = 0; }
bvhvec4::bvhvec4( const bvhvec3& a, float b ) { x = a.x; y = a.y; z = a.z; w = b; }
#endif

#ifdef _MSC_VER
#pragma warning ( pop )
#endif

// Math operations.
// Note: Since this header file is expected to be included in a source file
// of a separate project, the static keyword doesn't provide sufficient
// isolation; hence the tinybvh_ prefix.
inline float tinybvh_safercp( const float x ) { return x > 1e-12f ? (1.0f / x) : (x < -1e-12f ? (1.0f / x) : 1e30f); }
inline bvhvec3 tinybvh_safercp( const bvhvec3 a ) { return bvhvec3( tinybvh_safercp( a.x ), tinybvh_safercp( a.y ), tinybvh_safercp( a.z ) ); }
static inline float tinybvh_min( const float a, const float b ) { return a < b ? a : b; }
static inline float tinybvh_max( const float a, const float b ) { return a > b ? a : b; }
static inline int tinybvh_min( const int a, const int b ) { return a < b ? a : b; }
static inline int tinybvh_max( const int a, const int b ) { return a > b ? a : b; }
static inline unsigned tinybvh_min( const unsigned a, const unsigned b ) { return a < b ? a : b; }
static inline unsigned tinybvh_max( const unsigned a, const unsigned b ) { return a > b ? a : b; }
static inline bvhvec3 tinybvh_min( const bvhvec3& a, const bvhvec3& b ) { return bvhvec3( tinybvh_min( a.x, b.x ), tinybvh_min( a.y, b.y ), tinybvh_min( a.z, b.z ) ); }
static inline bvhvec4 tinybvh_min( const bvhvec4& a, const bvhvec4& b ) { return bvhvec4( tinybvh_min( a.x, b.x ), tinybvh_min( a.y, b.y ), tinybvh_min( a.z, b.z ), tinybvh_min( a.w, b.w ) ); }
static inline bvhvec3 tinybvh_max( const bvhvec3& a, const bvhvec3& b ) { return bvhvec3( tinybvh_max( a.x, b.x ), tinybvh_max( a.y, b.y ), tinybvh_max( a.z, b.z ) ); }
static inline bvhvec4 tinybvh_max( const bvhvec4& a, const bvhvec4& b ) { return bvhvec4( tinybvh_max( a.x, b.x ), tinybvh_max( a.y, b.y ), tinybvh_max( a.z, b.z ), tinybvh_max( a.w, b.w ) ); }
static inline float tinybvh_clamp( const float x, const float a, const float b ) { return x < a ? a : (x > b ? b : x); }
static inline int tinybvh_clamp( const int x, const int a, const int b ) { return x < a ? a : (x > b ? b : x); }
template <class T> inline static void tinybvh_swap( T& a, T& b ) { T t = a; a = b; b = t; }

// Operator overloads.
// Only a minimal set is provided.
inline bvhvec2 operator-( const bvhvec2& a ) { return bvhvec2( -a.x, -a.y ); }
inline bvhvec3 operator-( const bvhvec3& a ) { return bvhvec3( -a.x, -a.y, -a.z ); }
inline bvhvec4 operator-( const bvhvec4& a ) { return bvhvec4( -a.x, -a.y, -a.z, -a.w ); }
inline bvhvec2 operator+( const bvhvec2& a, const bvhvec2& b ) { return bvhvec2( a.x + b.x, a.y + b.y ); }
inline bvhvec3 operator+( const bvhvec3& a, const bvhvec3& b ) { return bvhvec3( a.x + b.x, a.y + b.y, a.z + b.z ); }
inline bvhvec4 operator+( const bvhvec4& a, const bvhvec4& b ) { return bvhvec4( a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w ); }
inline bvhvec2 operator-( const bvhvec2& a, const bvhvec2& b ) { return bvhvec2( a.x - b.x, a.y - b.y ); }
inline bvhvec3 operator-( const bvhvec3& a, const bvhvec3& b ) { return bvhvec3( a.x - b.x, a.y - b.y, a.z - b.z ); }
inline bvhvec4 operator-( const bvhvec4& a, const bvhvec4& b ) { return bvhvec4( a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w ); }
inline void operator+=( bvhvec2& a, const bvhvec2& b ) { a.x += b.x; a.y += b.y; }
inline void operator+=( bvhvec3& a, const bvhvec3& b ) { a.x += b.x; a.y += b.y; a.z += b.z; }
inline void operator+=( bvhvec4& a, const bvhvec4& b ) { a.x += b.x; a.y += b.y; a.z += b.z; a.w += b.w; }
inline bvhvec2 operator*( const bvhvec2& a, const bvhvec2& b ) { return bvhvec2( a.x * b.x, a.y * b.y ); }
inline bvhvec3 operator*( const bvhvec3& a, const bvhvec3& b ) { return bvhvec3( a.x * b.x, a.y * b.y, a.z * b.z ); }
inline bvhvec4 operator*( const bvhvec4& a, const bvhvec4& b ) { return bvhvec4( a.x * b.x, a.y * b.y, a.z * b.z, a.w * b.w ); }
inline bvhvec2 operator*( const bvhvec2& a, float b ) { return bvhvec2( a.x * b, a.y * b ); }
inline bvhvec2 operator*( float b, const bvhvec2& a ) { return bvhvec2( b * a.x, b * a.y ); }
inline bvhvec3 operator*( const bvhvec3& a, float b ) { return bvhvec3( a.x * b, a.y * b, a.z * b ); }
inline bvhvec3 operator*( float b, const bvhvec3& a ) { return bvhvec3( b * a.x, b * a.y, b * a.z ); }
inline bvhvec4 operator*( const bvhvec4& a, float b ) { return bvhvec4( a.x * b, a.y * b, a.z * b, a.w * b ); }
inline bvhvec4 operator*( float b, const bvhvec4& a ) { return bvhvec4( b * a.x, b * a.y, b * a.z, b * a.w ); }
inline bvhvec2 operator/( float b, const bvhvec2& a ) { return bvhvec2( b / a.x, b / a.y ); }
inline bvhvec3 operator/( float b, const bvhvec3& a ) { return bvhvec3( b / a.x, b / a.y, b / a.z ); }
inline bvhvec4 operator/( float b, const bvhvec4& a ) { return bvhvec4( b / a.x, b / a.y, b / a.z, b / a.w ); }
inline bvhvec3 operator*=( bvhvec3& a, const float b ) { return bvhvec3( a.x * b, a.y * b, a.z * b ); }

// Vector math: cross and dot.
static inline bvhvec3 cross( const bvhvec3& a, const bvhvec3& b )
{
	return bvhvec3( a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x );
}
static inline float dot( const bvhvec2& a, const bvhvec2& b ) { return a.x * b.x + a.y * b.y; }
static inline float dot( const bvhvec3& a, const bvhvec3& b ) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline float dot( const bvhvec4& a, const bvhvec4& b ) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }

// Vector math: common operations.
static float length( const bvhvec3& a ) { return sqrtf( a.x * a.x + a.y * a.y + a.z * a.z ); }
static bvhvec3 normalize( const bvhvec3& a )
{
	float l = length( a ), rl = l == 0 ? 0 : (1.0f / l);
	return a * rl;
}

// SIMD typedef, helps keeping the interface generic
#ifdef BVH_USEAVX
typedef __m128 SIMDVEC4;
#define SIMD_SETVEC(a,b,c,d) _mm_set_ps( a, b, c, d )
#define SIMD_SETRVEC(a,b,c,d) _mm_set_ps( d, c, b, a )
#elif defined(BVH_USENEON)
typedef float32x4_t SIMDVEC4;
inline float32x4_t SIMD_SETVEC( float w, float z, float y, float x )
{
	ALIGNED( 64 ) float data[4] = { x, y, z, w };
	return vld1q_f32( data );
}
inline float32x4_t SIMD_SETRVEC( float x, float y, float z, float w )
{
	ALIGNED( 64 ) float data[4] = { x, y, z, w };
	return vld1q_f32( data );
}
#else
typedef bvhvec4 SIMDVEC4;
#define SIMD_SETVEC(a,b,c,d) bvhvec4( d, c, b, a )
#define SIMD_SETRVEC(a,b,c,d) bvhvec4( a, b, c, d )
#endif

// error handling
#define FATAL_ERROR_IF(c,s) if (c) { fprintf( stderr, \
	"Fatal error in tiny_bvh.h, line %i:\n%s\n", __LINE__, s ); exit( 1 ); }

// ============================================================================
//
//        T I N Y _ B V H   I N T E R F A C E
// 
// ============================================================================

struct Intersection
{
	// An intersection result is designed to fit in no more than
	// four 32-bit values. This allows efficient storage of a result in
	// GPU code. The obvious missing result is an instance id; consider
	// squeezing this in the 'prim' field in some way.
	// Using this data and the original triangle data, all other info for
	// shading (such as normal, texture color etc.) can be reconstructed.
	float t, u, v;	// distance along ray & barycentric coordinates of the intersection
	unsigned prim;	// primitive index
};

struct Ray
{
	// Basic ray class. Note: For single blas traversal it is expected
	// that Ray::rD is properly initialized. For tlas/blas traversal this
	// field is typically updated for each blas.
	Ray() = default;
	Ray( bvhvec3 origin, bvhvec3 direction, float t = 1e30f )
	{
		memset( this, 0, sizeof( Ray ) );
		O = origin, D = normalize( direction ), rD = tinybvh_safercp( D );
		hit.t = t;
	}
	ALIGNED( 16 ) bvhvec3 O; unsigned dummy1;
	ALIGNED( 16 ) bvhvec3 D; unsigned dummy2;
	ALIGNED( 16 ) bvhvec3 rD; unsigned dummy3;
	ALIGNED( 16 ) Intersection hit;
};

struct BVHContext
{
	void* (*malloc)(size_t size, void* userdata) = malloc64;
	void (*free)(void* ptr, void* userdata) = free64;
	void* userdata = nullptr;
};

class BVH
{
public:
	enum BVHLayout {
		WALD_32BYTE = 1,	// Default format, obtained using BVH::Build variants.
		AILA_LAINE,			// For GPU rendering. Obtained by converting WALD_32BYTE.
		ALT_SOA,			// For faster CPU rendering. Obtained by converting WALD_32BYTE.
		VERBOSE,			// For BVH optimizing. Obtained by converting WALD_32BYTE.
		BASIC_BVH4,			// Input for BVH4_GPU conversion. Obtained by converting WALD_32BYTE.
		BVH4_GPU,			// For fast GPU rendering. Obtained by converting BASIC_BVH4.
		BVH4_AFRA,			// For fast CPU rendering. Obtained by converting BASIC_BVH4.
		BASIC_BVH8,			// Input for CWBVH. Obtained by converting WALD_32BYTE.
		CWBVH				// Fastest GPU rendering. Obtained by converting BASIC_BVH8.
	};
	enum TraceDevice {
		USE_CPU = 1,
		USE_GPU
	};
	enum BuildFlags {
		NONE = 0,			// Default building behavior (binned, SAH-driven).
		FULLSPLIT = 1		// Split as far as possible, even when SAH doesn't agree.
	};
	struct BVHNode
	{
		// 'Traditional' 32-byte BVH node layout, as proposed by Ingo Wald.
		// When aligned to a cache line boundary, two of these fit together.
		bvhvec3 aabbMin; unsigned leftFirst; // 16 bytes
		bvhvec3 aabbMax; unsigned triCount;	// 16 bytes, total: 32 bytes
		bool isLeaf() const { return triCount > 0; /* empty BVH leaves do not exist */ }
		float Intersect( const Ray& ray ) const { return BVH::IntersectAABB( ray, aabbMin, aabbMax ); }
		float SurfaceArea() const { return BVH::SA( aabbMin, aabbMax ); }
	};
	struct BVHNodeAlt
	{
		// Alternative 64-byte BVH node layout, which specifies the bounds of
		// the children rather than the node itself. This layout is used by
		// Aila and Laine in their seminal GPU ray tracing paper.
		bvhvec3 lmin; unsigned left;
		bvhvec3 lmax; unsigned right;
		bvhvec3 rmin; unsigned triCount;
		bvhvec3 rmax; unsigned firstTri; // total: 64 bytes
		bool isLeaf() const { return triCount > 0; }
	};
	struct BVHNodeAlt2
	{
		// Second alternative 64-byte BVH node layout, same as BVHNodeAlt but
		// with child AABBs stored in SoA order.
		SIMDVEC4 xxxx, yyyy, zzzz;
		unsigned left, right, triCount, firstTri; // total: 64 bytes
		bool isLeaf() const { return triCount > 0; }
	};
	struct BVHNodeVerbose
	{
		// This node layout has some extra data per node: It stores left and right
		// child node indices explicitly, and stores the index of the parent node.
		// This format exists primarily for the BVH optimizer.
		bvhvec3 aabbMin; unsigned left;
		bvhvec3 aabbMax; unsigned right;
		unsigned triCount, firstTri, parent, dummy;
		bool isLeaf() const { return triCount > 0; }
	};
	struct BVHNode4
	{
		// 4-wide (aka 'shallow') BVH layout.  
		bvhvec3 aabbMin; unsigned firstTri;
		bvhvec3 aabbMax; unsigned triCount;
		unsigned child[4];
		unsigned childCount, dummy1, dummy2, dummy3; // dummies are for alignment.
		bool isLeaf() const { return triCount > 0; }
	};
	struct BVHNode4Alt
	{
		// 4-way BVH node, optimized for GPU rendering
		struct aabb8 { unsigned char xmin, ymin, zmin, xmax, ymax, zmax; }; // quantized
		bvhvec3 aabbMin; unsigned c0Info;			// 16
		bvhvec3 aabbExt; unsigned c1Info;			// 16
		aabb8 c0bounds, c1bounds; unsigned c2Info;	// 16
		aabb8 c2bounds, c3bounds; unsigned c3Info;	// 16; total: 64 bytes
		// childInfo, 32bit:
		// msb:        0=interior, 1=leaf
		// leaf:       16 bits: relative start of triangle data, 15 bits: triangle count.
		// interior:   31 bits: child node address, in float4s from BVH data start.
		// Triangle data: directly follows nodes with leaves. Per tri:
		// - bvhvec4 vert0, vert1, vert2
		// - uint vert0.w stores original triangle index.
		// We can make the node smaller by storing child nodes sequentially, but
		// there is no way we can shave off a full 16 bytes, unless aabbExt is stored
		// as chars as well, as in CWBVH.
	};
	struct BVHNode4Alt2
	{
		// 4-way BVH node, optimized for CPU rendering.
		// Based on: "Faster Incoherent Ray Traversal Using 8-Wide AVX Instructions",
		// Áfra, 2013.
		SIMDVEC4 xmin4, ymin4, zmin4;
		SIMDVEC4 xmax4, ymax4, zmax4;
		unsigned childFirst[4];
		unsigned triCount[4];
	};
	struct BVHNode8
	{
		// 8-wide (aka 'shallow') BVH layout.  
		bvhvec3 aabbMin; unsigned firstTri;
		bvhvec3 aabbMax; unsigned triCount;
		unsigned child[8];
		unsigned childCount, dummy1, dummy2, dummy3; // dummies are for alignment.
		bool isLeaf() const { return triCount > 0; }
	};
	struct Fragment
	{
		// A fragment stores the bounds of an input primitive. The name 'Fragment' is from
		// "Parallel Spatial Splits in Bounding Volume Hierarchies", 2016, Fuetterling et al.,
		// and refers to the potential splitting of these boxes for SBVH construction.
		bvhvec3 bmin;				// AABB min x, y and z
		unsigned primIdx;		// index of the original primitive
		bvhvec3 bmax;				// AABB max x, y and z
		unsigned clipped = 0;	// Fragment is the result of clipping if > 0.
		bool validBox() { return bmin.x < 1e30f; }
	};
	BVH( BVHContext ctx = {} ) : context( ctx )
	{
	}
	~BVH()
	{
		AlignedFree( bvhNode );
		AlignedFree( altNode ); // Note: no action if pointer is null
		AlignedFree( alt2Node );
		AlignedFree( verbose );
		AlignedFree( bvh4Node );
		AlignedFree( bvh4Alt );
		AlignedFree( bvh8Node );
		AlignedFree( triIdx );
		AlignedFree( fragment );
		bvhNode = 0, triIdx = 0, fragment = 0;
		allocatedBVHNodes = 0;
		allocatedAltNodes = 0;
		allocatedAlt2Nodes = 0;
		allocatedVerbose = 0;
		allocatedBVH4Nodes = 0;
		allocatedAlt4aBlocks = 0;
		allocatedBVH8Nodes = 0;
	}
	float SAHCost( const unsigned nodeIdx = 0 ) const
	{
		// Determine the SAH cost of the tree. This provides an indication
		// of the quality of the BVH: Lower is better.
		const BVHNode& n = bvhNode[nodeIdx];
		if (n.isLeaf()) return 2.0f * n.SurfaceArea() * n.triCount;
		float cost = 3.0f * n.SurfaceArea() + SAHCost( n.leftFirst ) + SAHCost( n.leftFirst + 1 );
		return nodeIdx == 0 ? (cost / n.SurfaceArea()) : cost;
	}
	int NodeCount( const BVHLayout layout ) const;
	int PrimCount( const unsigned nodeIdx = 0 ) const
	{
		// Determine the total number of primitives / fragments in leaf nodes.
		const BVHNode& n = bvhNode[nodeIdx];
		return n.isLeaf() ? n.triCount : (PrimCount( n.leftFirst ) + PrimCount( n.leftFirst + 1 ));
	}
	void Compact( const BVHLayout layout /* must be WALD_32BYTE or VERBOSE */ );
	void Build( const bvhvec4* vertices, const unsigned primCount );
	void BuildHQ( const bvhvec4* vertices, const unsigned primCount );
#ifdef BVH_USEAVX
	void BuildAVX( const bvhvec4* vertices, const unsigned primCount );
#elif defined BVH_USENEON
	void BuildNEON( const bvhvec4* vertices, const unsigned primCount );
#endif
	void Convert( const BVHLayout from, const BVHLayout to, const bool deleteOriginal = false );
	void SplitLeafs( const unsigned maxPrims = 1 ); // operates on VERBOSE layout
	void SplitBVH8Leaf( const unsigned nodeIdx, const unsigned maxPrims = 1 ); // operates on BVH8 layout
	void MergeLeafs(); // operates on VERBOSE layout
	void Optimize( const unsigned iterations, const bool convertBack = true ); // operates on VERBOSE
	void Refit( const BVHLayout layout = WALD_32BYTE, const unsigned nodeIdx = 0 );
	int Intersect( Ray& ray, const BVHLayout layout = WALD_32BYTE ) const;
	bool IsOccluded( const Ray& ray, const BVHLayout layout = WALD_32BYTE ) const;
	void BatchIntersect( Ray* rayBatch, const unsigned N,
		const BVHLayout layout = WALD_32BYTE, const TraceDevice device = USE_CPU ) const;
	void BatchIsOccluded( Ray* rayBatch, const unsigned N, unsigned* result,
		const BVHLayout layout = WALD_32BYTE, const TraceDevice device = USE_CPU ) const;
	void Intersect256Rays( Ray* first ) const;
	void Intersect256RaysSSE( Ray* packet ) const; // requires BVH_USEAVX
private:
	void* AlignedAlloc( size_t size );
	void AlignedFree( void* ptr );
	int Intersect_Wald32Byte( Ray& ray ) const;
	int Intersect_AilaLaine( Ray& ray ) const;
	int Intersect_AltSoA( Ray& ray ) const; // requires BVH_USEAVX or BVH_USENEON
	int Intersect_BasicBVH4( Ray& ray ) const; // only for testing, not efficient.
	int Intersect_BasicBVH8( Ray& ray ) const; // only for testing, not efficient.
	int Intersect_Alt4BVH( Ray& ray ) const; // only for testing, not efficient.
	int Intersect_CWBVH( Ray& ray ) const; // only for testing, not efficient.
	bool IsOccluded_Wald32Byte( const Ray& ray ) const;
	void IntersectTri( Ray& ray, const unsigned triIdx ) const;
	static float IntersectAABB( const Ray& ray, const bvhvec3& aabbMin, const bvhvec3& aabbMax );
	static float SA( const bvhvec3& aabbMin, const bvhvec3& aabbMax )
	{
		bvhvec3 e = aabbMax - aabbMin; // extent of the node
		return e.x * e.y + e.y * e.z + e.z * e.x;
	}
	bool ClipFrag( const Fragment& orig, Fragment& newFrag, bvhvec3 bmin, bvhvec3 bmax, bvhvec3 minDim );
	void RefitUpVerbose( unsigned nodeIdx );
	unsigned FindBestNewPosition( const unsigned Lid );
	void ReinsertNodeVerbose( const unsigned Lid, const unsigned Nid, const unsigned origin );
	unsigned CountSubtreeTris( const unsigned nodeIdx, unsigned* counters );
	void MergeSubtree( const unsigned nodeIdx, unsigned* newIdx, unsigned& newIdxPtr );
public:
	BVHContext context;				// Context used to provide user-defined allocation functions
	bvhvec4* verts = 0;				// pointer to input primitive array: 3x16 bytes per tri
	unsigned triCount = 0;			// number of primitives in tris
	Fragment* fragment = 0;			// input primitive bounding boxes
	unsigned* triIdx = 0;			// primitive index array
	unsigned idxCount = 0;			// number of indices in triIdx. May exceed triCount * 3 for SBVH.
	BVHNode* bvhNode = 0;			// BVH node pool, Wald 32-byte format. Root is always in node 0.
	BVHNodeAlt* altNode = 0;		// BVH node in Aila & Laine format.
	BVHNodeAlt2* alt2Node = 0;		// BVH node in Aila & Laine (SoA version) format.
	BVHNodeVerbose* verbose = 0;	// BVH node with additional info, for BVH optimizer.
	BVHNode4* bvh4Node = 0;			// BVH node for 4-wide BVH.
	bvhvec4* bvh4Alt = 0;			// 64-byte 4-wide BVH node for efficient GPU rendering.
	BVHNode4Alt2* bvh4Alt2 = 0;		// 64-byte 4-wide BVH node for efficient CPU rendering.
	BVHNode8* bvh8Node = 0;			// BVH node for 8-wide BVH.
	bvhvec4* bvh8Compact = 0;		// Nodes in CWBVH format.
	bvhvec4* bvh8Tris = 0;			// Triangle data for CWBVH nodes.
	bool rebuildable = true;		// Rebuilds are safe only if a tree has not been converted.
	bool refittable = true;			// Refits are safe only if the tree has no spatial splits.
	bool frag_min_flipped = false;	// AVX builders flip aabb min.
	bool may_have_holes = false;	// Threaded builds and MergeLeafs produce BVHs with unused nodes.
	BuildFlags buildFlag = NONE;	// Hint to the builder.
	// keep track of allocated buffer size to avoid 
	// repeated allocation during layout conversion.
	unsigned allocatedBVHNodes = 0;
	unsigned allocatedAltNodes = 0;
	unsigned allocatedAlt2Nodes = 0;
	unsigned allocatedVerbose = 0;
	unsigned allocatedBVH4Nodes = 0;
	unsigned allocatedAlt4aBlocks = 0;
	unsigned allocatedAlt4bNodes = 0;
	unsigned allocatedBVH8Nodes = 0;
	unsigned allocatedCWBVHBlocks = 0;
	unsigned usedBVHNodes = 0;
	unsigned usedAltNodes = 0;
	unsigned usedAlt2Nodes = 0;
	unsigned usedVerboseNodes = 0;
	unsigned usedBVH4Nodes = 0;
	unsigned usedAlt4aBlocks = 0;
	unsigned usedAlt4bNodes = 0;
	unsigned usedBVH8Nodes = 0;
	unsigned usedCWBVHBlocks = 0;
};

} // namespace tinybvh

// ============================================================================
//
//        I M P L E M E N T A T I O N
// 
// ============================================================================

#ifdef TINYBVH_IMPLEMENTATION

#include <assert.h>			// for assert
#ifdef _MSC_VER
#include <intrin.h>			// for __lzcnt
#endif

namespace tinybvh {

void* BVH::AlignedAlloc( size_t size )
{
	return context.malloc ? context.malloc( size, context.userdata ) : nullptr;
}

void BVH::AlignedFree( void* ptr )
{
	if (context.free)
		context.free( ptr, context.userdata );
}

// Basic single-function binned-SAH-builder. 
// This is the reference builder; it yields a decent tree suitable for ray 
// tracing on the CPU. This code uses no SIMD instructions. 
// Faster code, using SSE/AVX, is available for x64 CPUs.
// For GPU rendering: The resulting BVH should be converted to a more optimal
// format after construction.
void BVH::Build( const bvhvec4* vertices, const unsigned primCount )
{
	// allocate on first build
	const unsigned spaceNeeded = primCount * 2; // upper limit
	if (allocatedBVHNodes < spaceNeeded)
	{
		AlignedFree( bvhNode );
		AlignedFree( triIdx );
		AlignedFree( fragment );
		bvhNode = (BVHNode*)AlignedAlloc( spaceNeeded * sizeof( BVHNode ) );
		allocatedBVHNodes = spaceNeeded;
		memset( &bvhNode[1], 0, 32 );	// node 1 remains unused, for cache line alignment.
		triIdx = (unsigned*)AlignedAlloc( primCount * sizeof( unsigned ) );
		verts = (bvhvec4*)vertices;		// note: we're not copying this data; don't delete.
		fragment = (Fragment*)AlignedAlloc( primCount * sizeof( Fragment ) );
	}
	else FATAL_ERROR_IF( !rebuildable, "BVH::Build( .. ), bvh not rebuildable." );
	idxCount = triCount = primCount;
	// reset node pool
	unsigned newNodePtr = 2;
	// assign all triangles to the root node
	BVHNode& root = bvhNode[0];
	root.leftFirst = 0, root.triCount = triCount, root.aabbMin = bvhvec3( 1e30f ), root.aabbMax = bvhvec3( -1e30f );
	// initialize fragments and initialize root node bounds
	for (unsigned i = 0; i < triCount; i++)
	{
		fragment[i].bmin = tinybvh_min( tinybvh_min( verts[i * 3], verts[i * 3 + 1] ), verts[i * 3 + 2] );
		fragment[i].bmax = tinybvh_max( tinybvh_max( verts[i * 3], verts[i * 3 + 1] ), verts[i * 3 + 2] );
		root.aabbMin = tinybvh_min( root.aabbMin, fragment[i].bmin );
		root.aabbMax = tinybvh_max( root.aabbMax, fragment[i].bmax ), triIdx[i] = i;
	}
	// subdivide recursively
	unsigned task[256], taskCount = 0, nodeIdx = 0;
	bvhvec3 minDim = (root.aabbMax - root.aabbMin) * 1e-20f, bestLMin = 0, bestLMax = 0, bestRMin = 0, bestRMax = 0;
	while (1)
	{
		while (1)
		{
			BVHNode& node = bvhNode[nodeIdx];
			// find optimal object split
			bvhvec3 binMin[3][BVHBINS], binMax[3][BVHBINS];
			for (unsigned a = 0; a < 3; a++) for (unsigned i = 0; i < BVHBINS; i++) binMin[a][i] = 1e30f, binMax[a][i] = -1e30f;
			unsigned count[3][BVHBINS];
			memset( count, 0, BVHBINS * 3 * sizeof( unsigned ) );
			const bvhvec3 rpd3 = bvhvec3( BVHBINS / (node.aabbMax - node.aabbMin) ), nmin3 = node.aabbMin;
			for (unsigned i = 0; i < node.triCount; i++) // process all tris for x,y and z at once
			{
				const unsigned fi = triIdx[node.leftFirst + i];
				bvhint3 bi = bvhint3( ((fragment[fi].bmin + fragment[fi].bmax) * 0.5f - nmin3) * rpd3 );
				bi.x = tinybvh_clamp( bi.x, 0, BVHBINS - 1 );
				bi.y = tinybvh_clamp( bi.y, 0, BVHBINS - 1 );
				bi.z = tinybvh_clamp( bi.z, 0, BVHBINS - 1 );
				binMin[0][bi.x] = tinybvh_min( binMin[0][bi.x], fragment[fi].bmin );
				binMax[0][bi.x] = tinybvh_max( binMax[0][bi.x], fragment[fi].bmax ), count[0][bi.x]++;
				binMin[1][bi.y] = tinybvh_min( binMin[1][bi.y], fragment[fi].bmin );
				binMax[1][bi.y] = tinybvh_max( binMax[1][bi.y], fragment[fi].bmax ), count[1][bi.y]++;
				binMin[2][bi.z] = tinybvh_min( binMin[2][bi.z], fragment[fi].bmin );
				binMax[2][bi.z] = tinybvh_max( binMax[2][bi.z], fragment[fi].bmax ), count[2][bi.z]++;
			}
			// calculate per-split totals
			float splitCost = 1e30f, rSAV = 1.0f / node.SurfaceArea();
			unsigned bestAxis = 0, bestPos = 0;
			for (int a = 0; a < 3; a++) if ((node.aabbMax[a] - node.aabbMin[a]) > minDim[a])
			{
				bvhvec3 lBMin[BVHBINS - 1], rBMin[BVHBINS - 1], l1 = 1e30f, l2 = -1e30f;
				bvhvec3 lBMax[BVHBINS - 1], rBMax[BVHBINS - 1], r1 = 1e30f, r2 = -1e30f;
				float ANL[BVHBINS - 1], ANR[BVHBINS - 1];
				for (unsigned lN = 0, rN = 0, i = 0; i < BVHBINS - 1; i++)
				{
					lBMin[i] = l1 = tinybvh_min( l1, binMin[a][i] );
					rBMin[BVHBINS - 2 - i] = r1 = tinybvh_min( r1, binMin[a][BVHBINS - 1 - i] );
					lBMax[i] = l2 = tinybvh_max( l2, binMax[a][i] );
					rBMax[BVHBINS - 2 - i] = r2 = tinybvh_max( r2, binMax[a][BVHBINS - 1 - i] );
					lN += count[a][i], rN += count[a][BVHBINS - 1 - i];
					ANL[i] = lN == 0 ? 1e30f : ((l2 - l1).halfArea() * (float)lN);
					ANR[BVHBINS - 2 - i] = rN == 0 ? 1e30f : ((r2 - r1).halfArea() * (float)rN);
				}
				// evaluate bin totals to find best position for object split
				for (unsigned i = 0; i < BVHBINS - 1; i++)
				{
					const float C = C_TRAV + rSAV * C_INT * (ANL[i] + ANR[i]);
					if (C < splitCost)
					{
						splitCost = C, bestAxis = a, bestPos = i;
						bestLMin = lBMin[i], bestRMin = rBMin[i], bestLMax = lBMax[i], bestRMax = rBMax[i];
					}
				}
			}
			float noSplitCost = (float)node.triCount * C_INT;
			if (splitCost >= noSplitCost) break; // not splitting is better.
			// in-place partition
			unsigned j = node.leftFirst + node.triCount, src = node.leftFirst;
			const float rpd = rpd3.cell[bestAxis], nmin = nmin3.cell[bestAxis];
			for (unsigned i = 0; i < node.triCount; i++)
			{
				const unsigned fi = triIdx[src];
				int bi = (unsigned)(((fragment[fi].bmin[bestAxis] + fragment[fi].bmax[bestAxis]) * 0.5f - nmin) * rpd);
				bi = tinybvh_clamp( bi, 0, BVHBINS - 1 );
				if ((unsigned)bi <= bestPos) src++; else tinybvh_swap( triIdx[src], triIdx[--j] );
			}
			// create child nodes
			unsigned leftCount = src - node.leftFirst, rightCount = node.triCount - leftCount;
			if (leftCount == 0 || rightCount == 0) break; // should not happen.
			const int lci = newNodePtr++, rci = newNodePtr++;
			bvhNode[lci].aabbMin = bestLMin, bvhNode[lci].aabbMax = bestLMax;
			bvhNode[lci].leftFirst = node.leftFirst, bvhNode[lci].triCount = leftCount;
			bvhNode[rci].aabbMin = bestRMin, bvhNode[rci].aabbMax = bestRMax;
			bvhNode[rci].leftFirst = j, bvhNode[rci].triCount = rightCount;
			node.leftFirst = lci, node.triCount = 0;
			// recurse
			task[taskCount++] = rci, nodeIdx = lci;
		}
		// fetch subdivision task from stack
		if (taskCount == 0) break; else nodeIdx = task[--taskCount];
	}
	// all done.
	refittable = true; // not using spatial splits: can refit this BVH
	frag_min_flipped = false; // did not use AVX for binning
	may_have_holes = false; // the reference builder produces a continuous list of nodes
	usedBVHNodes = newNodePtr;
}

// SBVH builder.
// Besides the regular object splits used in the reference builder, the SBVH
// algorithm also considers spatial splits, where primitives may be cut in
// multiple parts. This increases primitive count but may reduce overlap of
// BVH nodes. The cost of each option is considered per split. 
// For typical geometry, SBVH yields a tree that can be traversed 25% faster. 
// This comes at greatly increased construction cost, making the SBVH 
// primarily useful for static geometry.
void BVH::BuildHQ( const bvhvec4* vertices, const unsigned primCount )
{
	// allocate on first build
	const unsigned slack = primCount >> 2; // for split prims
	const unsigned spaceNeeded = primCount * 3;
	if (allocatedBVHNodes < spaceNeeded)
	{
		AlignedFree( bvhNode );
		AlignedFree( triIdx );
		AlignedFree( fragment );
		bvhNode = (BVHNode*)AlignedAlloc( spaceNeeded * sizeof( BVHNode ) );
		allocatedBVHNodes = spaceNeeded;
		memset( &bvhNode[1], 0, 32 );	// node 1 remains unused, for cache line alignment.
		triIdx = (unsigned*)AlignedAlloc( (primCount + slack) * sizeof( unsigned ) );
		verts = (bvhvec4*)vertices;		// note: we're not copying this data; don't delete.
		fragment = (Fragment*)AlignedAlloc( (primCount + slack) * sizeof( Fragment ) );
	}
	else FATAL_ERROR_IF( !rebuildable, "BVH::BuildHQ( .. ), bvh not rebuildable." );
	idxCount = primCount + slack;
	triCount = primCount;
	unsigned* triIdxA = triIdx, * triIdxB = new unsigned[triCount + slack];
	memset( triIdxA, 0, (triCount + slack) * 4 );
	memset( triIdxB, 0, (triCount + slack) * 4 );
	// reset node pool
	unsigned newNodePtr = 2, nextFrag = triCount;
	// assign all triangles to the root node
	BVHNode& root = bvhNode[0];
	root.leftFirst = 0, root.triCount = triCount, root.aabbMin = bvhvec3( 1e30f ), root.aabbMax = bvhvec3( -1e30f );
	// initialize fragments and initialize root node bounds
	for (unsigned i = 0; i < triCount; i++)
	{
		fragment[i].bmin = tinybvh_min( tinybvh_min( verts[i * 3], verts[i * 3 + 1] ), verts[i * 3 + 2] );
		fragment[i].bmax = tinybvh_max( tinybvh_max( verts[i * 3], verts[i * 3 + 1] ), verts[i * 3 + 2] );
		root.aabbMin = tinybvh_min( root.aabbMin, fragment[i].bmin );
		root.aabbMax = tinybvh_max( root.aabbMax, fragment[i].bmax ), triIdx[i] = i, fragment[i].primIdx = i;
	}
	const float rootArea = (root.aabbMax - root.aabbMin).halfArea();
	// subdivide recursively
	struct Task { unsigned node, sliceStart, sliceEnd, dummy; };
	ALIGNED( 64 ) Task task[256];
	unsigned taskCount = 0, nodeIdx = 0, sliceStart = 0, sliceEnd = triCount + slack;
	const bvhvec3 minDim = (root.aabbMax - root.aabbMin) * 1e-7f /* don't touch, carefully picked */;
	bvhvec3 bestLMin = 0, bestLMax = 0, bestRMin = 0, bestRMax = 0;
	while (1)
	{
		while (1)
		{
			BVHNode& node = bvhNode[nodeIdx];
			// find optimal object split
			bvhvec3 binMin[3][BVHBINS], binMax[3][BVHBINS];
			for (unsigned a = 0; a < 3; a++) for (unsigned i = 0; i < BVHBINS; i++) binMin[a][i] = 1e30f, binMax[a][i] = -1e30f;
			unsigned count[3][BVHBINS];
			memset( count, 0, BVHBINS * 3 * sizeof( unsigned ) );
			const bvhvec3 rpd3 = bvhvec3( BVHBINS / (node.aabbMax - node.aabbMin) ), nmin3 = node.aabbMin;
			for (unsigned i = 0; i < node.triCount; i++) // process all tris for x,y and z at once
			{
				const unsigned fi = triIdx[node.leftFirst + i];
				bvhint3 bi = bvhint3( ((fragment[fi].bmin + fragment[fi].bmax) * 0.5f - nmin3) * rpd3 );
				bi.x = tinybvh_clamp( bi.x, 0, BVHBINS - 1 );
				bi.y = tinybvh_clamp( bi.y, 0, BVHBINS - 1 );
				bi.z = tinybvh_clamp( bi.z, 0, BVHBINS - 1 );
				binMin[0][bi.x] = tinybvh_min( binMin[0][bi.x], fragment[fi].bmin );
				binMax[0][bi.x] = tinybvh_max( binMax[0][bi.x], fragment[fi].bmax ), count[0][bi.x]++;
				binMin[1][bi.y] = tinybvh_min( binMin[1][bi.y], fragment[fi].bmin );
				binMax[1][bi.y] = tinybvh_max( binMax[1][bi.y], fragment[fi].bmax ), count[1][bi.y]++;
				binMin[2][bi.z] = tinybvh_min( binMin[2][bi.z], fragment[fi].bmin );
				binMax[2][bi.z] = tinybvh_max( binMax[2][bi.z], fragment[fi].bmax ), count[2][bi.z]++;
			}
			// calculate per-split totals
			float splitCost = 1e30f, rSAV = 1.0f / node.SurfaceArea();
			unsigned bestAxis = 0, bestPos = 0;
			for (int a = 0; a < 3; a++) if ((node.aabbMax[a] - node.aabbMin[a]) > minDim.cell[a])
			{
				bvhvec3 lBMin[BVHBINS - 1], rBMin[BVHBINS - 1], l1 = 1e30f, l2 = -1e30f;
				bvhvec3 lBMax[BVHBINS - 1], rBMax[BVHBINS - 1], r1 = 1e30f, r2 = -1e30f;
				float ANL[BVHBINS - 1], ANR[BVHBINS - 1];
				for (unsigned lN = 0, rN = 0, i = 0; i < BVHBINS - 1; i++)
				{
					lBMin[i] = l1 = tinybvh_min( l1, binMin[a][i] );
					rBMin[BVHBINS - 2 - i] = r1 = tinybvh_min( r1, binMin[a][BVHBINS - 1 - i] );
					lBMax[i] = l2 = tinybvh_max( l2, binMax[a][i] );
					rBMax[BVHBINS - 2 - i] = r2 = tinybvh_max( r2, binMax[a][BVHBINS - 1 - i] );
					lN += count[a][i], rN += count[a][BVHBINS - 1 - i];
					ANL[i] = lN == 0 ? 1e30f : ((l2 - l1).halfArea() * (float)lN);
					ANR[BVHBINS - 2 - i] = rN == 0 ? 1e30f : ((r2 - r1).halfArea() * (float)rN);
				}
				// evaluate bin totals to find best position for object split
				for (unsigned i = 0; i < BVHBINS - 1; i++)
				{
					const float C = C_TRAV + C_INT * rSAV * (ANL[i] + ANR[i]);
					if (C < splitCost)
					{
						splitCost = C, bestAxis = a, bestPos = i;
						bestLMin = lBMin[i], bestRMin = rBMin[i], bestLMax = lBMax[i], bestRMax = rBMax[i];
					}
				}
			}
			// consider a spatial split
			bool spatial = false;
			unsigned NL[BVHBINS - 1], NR[BVHBINS - 1], budget = sliceEnd - sliceStart;
			bvhvec3 spatialUnion = bestLMax - bestRMin;
			float spatialOverlap = (spatialUnion.halfArea()) / rootArea;
			if (budget > node.triCount && splitCost < 1e30f && spatialOverlap > 1e-5f)
			{
				for (unsigned a = 0; a < 3; a++) if ((node.aabbMax[a] - node.aabbMin[a]) > minDim.cell[a])
				{
					// setup bins
					bvhvec3 binMin[BVHBINS], binMax[BVHBINS];
					for (unsigned i = 0; i < BVHBINS; i++) binMin[i] = 1e30f, binMax[i] = -1e30f;
					unsigned countIn[BVHBINS] = { 0 }, countOut[BVHBINS] = { 0 };
					// populate bins with clipped fragments
					const float planeDist = (node.aabbMax[a] - node.aabbMin[a]) / (BVHBINS * 0.9999f);
					const float rPlaneDist = 1.0f / planeDist, nodeMin = node.aabbMin[a];
					for (unsigned i = 0; i < node.triCount; i++)
					{
						const unsigned fragIdx = triIdxA[node.leftFirst + i];
						const int bin1 = tinybvh_clamp( (int)((fragment[fragIdx].bmin[a] - nodeMin) * rPlaneDist), 0, BVHBINS - 1 );
						const int bin2 = tinybvh_clamp( (int)((fragment[fragIdx].bmax[a] - nodeMin) * rPlaneDist), 0, BVHBINS - 1 );
						countIn[bin1]++, countOut[bin2]++;
						if (bin2 == bin1)
						{
							// fragment fits in a single bin
							binMin[bin1] = tinybvh_min( binMin[bin1], fragment[fragIdx].bmin );
							binMax[bin1] = tinybvh_max( binMax[bin1], fragment[fragIdx].bmax );
						}
						else for (int j = bin1; j <= bin2; j++)
						{
							// clip fragment to each bin it overlaps
							bvhvec3 bmin = node.aabbMin, bmax = node.aabbMax;
							bmin[a] = nodeMin + planeDist * j;
							bmax[a] = j == 6 ? node.aabbMax[a] : (bmin[a] + planeDist);
							Fragment orig = fragment[fragIdx];
							Fragment tmpFrag;
							if (!ClipFrag( orig, tmpFrag, bmin, bmax, minDim )) continue;
							binMin[j] = tinybvh_min( binMin[j], tmpFrag.bmin );
							binMax[j] = tinybvh_max( binMax[j], tmpFrag.bmax );
						}
					}
					// evaluate split candidates
					bvhvec3 lBMin[BVHBINS - 1], rBMin[BVHBINS - 1], l1 = 1e30f, l2 = -1e30f;
					bvhvec3 lBMax[BVHBINS - 1], rBMax[BVHBINS - 1], r1 = 1e30f, r2 = -1e30f;
					float ANL[BVHBINS], ANR[BVHBINS];
					for (unsigned lN = 0, rN = 0, i = 0; i < BVHBINS - 1; i++)
					{
						lBMin[i] = l1 = tinybvh_min( l1, binMin[i] ), rBMin[BVHBINS - 2 - i] = r1 = tinybvh_min( r1, binMin[BVHBINS - 1 - i] );
						lBMax[i] = l2 = tinybvh_max( l2, binMax[i] ), rBMax[BVHBINS - 2 - i] = r2 = tinybvh_max( r2, binMax[BVHBINS - 1 - i] );
						lN += countIn[i], rN += countOut[BVHBINS - 1 - i], NL[i] = lN, NR[BVHBINS - 2 - i] = rN;
						ANL[i] = lN == 0 ? 1e30f : ((l2 - l1).halfArea() * (float)lN);
						ANR[BVHBINS - 2 - i] = rN == 0 ? 1e30f : ((r2 - r1).halfArea() * (float)rN);
					}
					// find best position for spatial split
					for (unsigned i = 0; i < BVHBINS - 1; i++)
					{
						const float Cspatial = C_TRAV + C_INT * rSAV * (ANL[i] + ANR[i]);
						if (Cspatial < splitCost && NL[i] + NR[i] < budget)
						{
							spatial = true, splitCost = Cspatial, bestAxis = a, bestPos = i;
							bestLMin = lBMin[i], bestLMax = lBMax[i], bestRMin = rBMin[i], bestRMax = rBMax[i];
							bestLMax[a] = bestRMin[a]; // accurate
						}
					}
				}
			}
			// terminate recursion
			float noSplitCost = (float)node.triCount * C_INT;
			if (splitCost >= noSplitCost) break; // not splitting is better.
			// double-buffered partition
			unsigned A = sliceStart, B = sliceEnd, src = node.leftFirst;
			if (spatial)
			{
				const float planeDist = (node.aabbMax[bestAxis] - node.aabbMin[bestAxis]) / (BVHBINS * 0.9999f);
				const float rPlaneDist = 1.0f / planeDist, nodeMin = node.aabbMin[bestAxis];
				for (unsigned i = 0; i < node.triCount; i++)
				{
					const unsigned fragIdx = triIdxA[src++];
					const unsigned bin1 = (unsigned)((fragment[fragIdx].bmin[bestAxis] - nodeMin) * rPlaneDist);
					const unsigned bin2 = (unsigned)((fragment[fragIdx].bmax[bestAxis] - nodeMin) * rPlaneDist);
					if (bin2 <= bestPos) triIdxB[A++] = fragIdx; else if (bin1 > bestPos) triIdxB[--B] = fragIdx; else
					{
						// split straddler
						Fragment tmpFrag = fragment[fragIdx];
						Fragment newFrag;
						if (ClipFrag( tmpFrag, newFrag, tinybvh_max( bestRMin, node.aabbMin ), tinybvh_min( bestRMax, node.aabbMax ), minDim ))
							fragment[nextFrag] = newFrag, triIdxB[--B] = nextFrag++;
						if (ClipFrag( tmpFrag, fragment[fragIdx], tinybvh_max( bestLMin, node.aabbMin ), tinybvh_min( bestLMax, node.aabbMax ), minDim ))
							triIdxB[A++] = fragIdx;
					}
				}
			}
			else
			{
				// object partitioning
				const float rpd = rpd3.cell[bestAxis], nmin = nmin3.cell[bestAxis];
				for (unsigned i = 0; i < node.triCount; i++)
				{
					const unsigned fr = triIdx[src + i];
					int bi = (int)(((fragment[fr].bmin[bestAxis] + fragment[fr].bmax[bestAxis]) * 0.5f - nmin) * rpd);
					bi = tinybvh_clamp( bi, 0, BVHBINS - 1 );
					if (bi <= (int)bestPos) triIdxB[A++] = fr; else triIdxB[--B] = fr;
				}
			}
			// copy back slice data
			memcpy( triIdxA + sliceStart, triIdxB + sliceStart, (sliceEnd - sliceStart) * 4 );
			// create child nodes
			unsigned leftCount = A - sliceStart, rightCount = sliceEnd - B;
			if (leftCount == 0 || rightCount == 0) break;
			int leftChildIdx = newNodePtr++, rightChildIdx = newNodePtr++;
			bvhNode[leftChildIdx].aabbMin = bestLMin, bvhNode[leftChildIdx].aabbMax = bestLMax;
			bvhNode[leftChildIdx].leftFirst = sliceStart, bvhNode[leftChildIdx].triCount = leftCount;
			bvhNode[rightChildIdx].aabbMin = bestRMin, bvhNode[rightChildIdx].aabbMax = bestRMax;
			bvhNode[rightChildIdx].leftFirst = B, bvhNode[rightChildIdx].triCount = rightCount;
			node.leftFirst = leftChildIdx, node.triCount = 0;
			// recurse
			task[taskCount].node = rightChildIdx;
			task[taskCount].sliceEnd = sliceEnd;
			task[taskCount++].sliceStart = sliceEnd = (A + B) >> 1;
			nodeIdx = leftChildIdx;
		}
		// fetch subdivision task from stack
		if (taskCount == 0) break; else
			nodeIdx = task[--taskCount].node,
			sliceStart = task[taskCount].sliceStart,
			sliceEnd = task[taskCount].sliceEnd;
	}
	// clean up
	for (unsigned i = 0; i < triCount + slack; i++) triIdx[i] = fragment[triIdx[i]].primIdx;
	// Compact(); - TODO
	// all done.
	refittable = false; // can't refit an SBVH
	frag_min_flipped = false; // did not use AVX for binning
	may_have_holes = false; // there may be holes in the index list, but not in the node list
	usedBVHNodes = newNodePtr;
}

bool BVH::ClipFrag( const Fragment& orig, Fragment& newFrag, bvhvec3 bmin, bvhvec3 bmax, bvhvec3 minDim )
{
	// find intersection of bmin/bmax and orig bmin/bmax
	bmin = tinybvh_max( bmin, orig.bmin );
	bmax = tinybvh_min( bmax, orig.bmax );
	const bvhvec3 extent = bmax - bmin;
	// Sutherland-Hodgeman against six bounding planes
	unsigned Nin = 3, vidx = orig.primIdx * 3;
	bvhvec3 vin[10] = { verts[vidx], verts[vidx + 1], verts[vidx + 2] }, vout[10];
	for (unsigned a = 0; a < 3; a++)
	{
		const float eps = minDim.cell[a];
		if (extent.cell[a] > eps)
		{
			unsigned Nout = 0;
			const float l = bmin[a], r = bmax[a];
			for (unsigned v = 0; v < Nin; v++)
			{
				bvhvec3 v0 = vin[v], v1 = vin[(v + 1) % Nin];
				const bool v0in = v0[a] >= l - eps, v1in = v1[a] >= l - eps;
				if (!(v0in || v1in)) continue; else if (v0in != v1in)
				{
					bvhvec3 C = v0 + (l - v0[a]) / (v1[a] - v0[a]) * (v1 - v0);
					C[a] = l /* accurate */, vout[Nout++] = C;
				}
				if (v1in) vout[Nout++] = v1;
			}
			Nin = 0;
			for (unsigned v = 0; v < Nout; v++)
			{
				bvhvec3 v0 = vout[v], v1 = vout[(v + 1) % Nout];
				const bool v0in = v0[a] <= r + eps, v1in = v1[a] <= r + eps;
				if (!(v0in || v1in)) continue; else if (v0in != v1in)
				{
					bvhvec3 C = v0 + (r - v0[a]) / (v1[a] - v0[a]) * (v1 - v0);
					C[a] = r /* accurate */, vin[Nin++] = C;
				}
				if (v1in) vin[Nin++] = v1;
			}
		}
	}
	bvhvec3 mn( 1e30f ), mx( -1e30f );
	for (unsigned i = 0; i < Nin; i++) mn = tinybvh_min( mn, vin[i] ), mx = tinybvh_max( mx, vin[i] );
	newFrag.primIdx = orig.primIdx;
	newFrag.bmin = tinybvh_max( mn, bmin ), newFrag.bmax = tinybvh_min( mx, bmax );
	newFrag.clipped = 1;
	return Nin > 0;
}

// Convert: Change the BVH layout from one format into another.
void BVH::Convert( const BVHLayout from, const BVHLayout to, const bool deleteOriginal )
{
	if (from == WALD_32BYTE && to == AILA_LAINE)
	{
		// allocate space
		const unsigned spaceNeeded = usedBVHNodes;
		if (allocatedAltNodes < spaceNeeded)
		{
			FATAL_ERROR_IF( bvhNode == 0, "BVH::Convert( WALD_32BYTE, AILA_LAINE ), bvhNode == 0." );
			AlignedFree( altNode );
			altNode = (BVHNodeAlt*)AlignedAlloc( sizeof( BVHNodeAlt ) * spaceNeeded );
			allocatedAltNodes = spaceNeeded;
		}
		memset( altNode, 0, sizeof( BVHNodeAlt ) * spaceNeeded );
		// recursively convert nodes
		unsigned newAltNode = 0, nodeIdx = 0, stack[128], stackPtr = 0;
		while (1)
		{
			const BVHNode& node = bvhNode[nodeIdx];
			const unsigned idx = newAltNode++;
			if (node.isLeaf())
			{
				altNode[idx].triCount = node.triCount;
				altNode[idx].firstTri = node.leftFirst;
				if (!stackPtr) break;
				nodeIdx = stack[--stackPtr];
				unsigned newNodeParent = stack[--stackPtr];
				altNode[newNodeParent].right = newAltNode;
			}
			else
			{
				const BVHNode& left = bvhNode[node.leftFirst];
				const BVHNode& right = bvhNode[node.leftFirst + 1];
				altNode[idx].lmin = left.aabbMin, altNode[idx].rmin = right.aabbMin;
				altNode[idx].lmax = left.aabbMax, altNode[idx].rmax = right.aabbMax;
				altNode[idx].left = newAltNode; // right will be filled when popped
				stack[stackPtr++] = idx;
				stack[stackPtr++] = node.leftFirst + 1;
				nodeIdx = node.leftFirst;
			}
		}
		usedAltNodes = newAltNode;
	}
	else if (from == WALD_32BYTE && to == ALT_SOA)
	{
		// allocate space
		const unsigned spaceNeeded = usedBVHNodes;
		if (allocatedAlt2Nodes < spaceNeeded)
		{
			FATAL_ERROR_IF( bvhNode == 0, "BVH::Convert( WALD_32BYTE, ALT_SOA ), bvhNode == 0." );
			AlignedFree( alt2Node );
			alt2Node = (BVHNodeAlt2*)AlignedAlloc( sizeof( BVHNodeAlt2 ) * spaceNeeded );
			allocatedAlt2Nodes = spaceNeeded;
		}
		memset( alt2Node, 0, sizeof( BVHNodeAlt2 ) * spaceNeeded );
		// recursively convert nodes
		unsigned newAlt2Node = 0, nodeIdx = 0, stack[128], stackPtr = 0;
		while (1)
		{
			const BVHNode& node = bvhNode[nodeIdx];
			const unsigned idx = newAlt2Node++;
			if (node.isLeaf())
			{
				alt2Node[idx].triCount = node.triCount;
				alt2Node[idx].firstTri = node.leftFirst;
				if (!stackPtr) break;
				nodeIdx = stack[--stackPtr];
				unsigned newNodeParent = stack[--stackPtr];
				alt2Node[newNodeParent].right = newAlt2Node;
			}
			else
			{
				const BVHNode& left = bvhNode[node.leftFirst];
				const BVHNode& right = bvhNode[node.leftFirst + 1];
				// This BVH layout requires BVH_USEAVX/BVH_USENEON for traversal, but at least we
				// can convert to it without SSE/AVX/NEON support.
				alt2Node[idx].xxxx = SIMD_SETRVEC( left.aabbMin.x, left.aabbMax.x, right.aabbMin.x, right.aabbMax.x );
				alt2Node[idx].yyyy = SIMD_SETRVEC( left.aabbMin.y, left.aabbMax.y, right.aabbMin.y, right.aabbMax.y );
				alt2Node[idx].zzzz = SIMD_SETRVEC( left.aabbMin.z, left.aabbMax.z, right.aabbMin.z, right.aabbMax.z );
				alt2Node[idx].left = newAlt2Node; // right will be filled when popped
				stack[stackPtr++] = idx;
				stack[stackPtr++] = node.leftFirst + 1;
				nodeIdx = node.leftFirst;
			}
		}
		usedAlt2Nodes = newAlt2Node;
	}
	else if (from == WALD_32BYTE && to == VERBOSE)
	{
		// allocate space
		unsigned spaceNeeded = triCount * (refittable ? 2 : 3); // this one needs space to grow to 2N
		if (allocatedVerbose < spaceNeeded)
		{
			FATAL_ERROR_IF( bvhNode == 0, "BVH::Convert( WALD_32BYTE, VERBOSE ), bvhNode == 0." );
			AlignedFree( verbose );
			verbose = (BVHNodeVerbose*)AlignedAlloc( sizeof( BVHNodeVerbose ) * spaceNeeded );
			allocatedVerbose = spaceNeeded;
		}
		memset( verbose, 0, sizeof( BVHNodeVerbose ) * spaceNeeded );
		verbose[0].parent = 0xffffffff; // root sentinel
		// convert
		unsigned nodeIdx = 0, parent = 0xffffffff, stack[128], stackPtr = 0;
		while (1)
		{
			const BVHNode& node = bvhNode[nodeIdx];
			verbose[nodeIdx].aabbMin = node.aabbMin, verbose[nodeIdx].aabbMax = node.aabbMax;
			verbose[nodeIdx].triCount = node.triCount, verbose[nodeIdx].parent = parent;
			if (node.isLeaf())
			{
				verbose[nodeIdx].firstTri = node.leftFirst;
				if (stackPtr == 0) break;
				nodeIdx = stack[--stackPtr];
				parent = stack[--stackPtr];
			}
			else
			{
				verbose[nodeIdx].left = node.leftFirst;
				verbose[nodeIdx].right = node.leftFirst + 1;
				stack[stackPtr++] = nodeIdx;
				stack[stackPtr++] = node.leftFirst + 1;
				parent = nodeIdx;
				nodeIdx = node.leftFirst;
			}
		}
		usedVerboseNodes = usedBVHNodes;
	}
	else if (from == WALD_32BYTE && to == BASIC_BVH4)
	{
		// allocate space
		const unsigned spaceNeeded = usedBVHNodes;
		if (allocatedBVH4Nodes < spaceNeeded)
		{
			FATAL_ERROR_IF( bvhNode == 0, "BVH::Convert( WALD_32BYTE, BASIC_BVH4 ), bvhNode == 0." );
			AlignedFree( bvh4Node );
			bvh4Node = (BVHNode4*)AlignedAlloc( spaceNeeded * sizeof( BVHNode4 ) );
			allocatedBVH4Nodes = spaceNeeded;
		}
		memset( bvh4Node, 0, sizeof( BVHNode4 ) * spaceNeeded );
		// create an mbvh node for each bvh2 node
		for (unsigned i = 0; i < usedBVHNodes; i++) if (i != 1)
		{
			BVHNode& orig = bvhNode[i];
			BVHNode4& node4 = bvh4Node[i];
			node4.aabbMin = orig.aabbMin, node4.aabbMax = orig.aabbMax;
			if (orig.isLeaf()) node4.triCount = orig.triCount, node4.firstTri = orig.leftFirst;
			else node4.child[0] = orig.leftFirst, node4.child[1] = orig.leftFirst + 1, node4.childCount = 2;
		}
		// collapse
		unsigned stack[128], stackPtr = 1, nodeIdx = stack[0] = 0; // i.e., root node
		while (1)
		{
			BVHNode4& node = bvh4Node[nodeIdx];
			while (node.childCount < 4)
			{
				int bestChild = -1;
				float bestChildSA = 0;
				for (unsigned i = 0; i < node.childCount; i++)
				{
					// see if we can adopt child i
					const BVHNode4& child = bvh4Node[node.child[i]];
					if (!child.isLeaf() && node.childCount - 1 + child.childCount <= 4)
					{
						const float childSA = SA( child.aabbMin, child.aabbMax );
						if (childSA > bestChildSA) bestChild = i, bestChildSA = childSA;
					}
				}
				if (bestChild == -1) break; // could not adopt
				const BVHNode4& child = bvh4Node[node.child[bestChild]];
				node.child[bestChild] = child.child[0];
				for (unsigned i = 1; i < child.childCount; i++)
					node.child[node.childCount++] = child.child[i];
			}
			// we're done with the node; proceed with the children
			for (unsigned i = 0; i < node.childCount; i++)
			{
				const unsigned childIdx = node.child[i];
				const BVHNode4& child = bvh4Node[childIdx];
				if (!child.isLeaf()) stack[stackPtr++] = childIdx;
			}
			if (stackPtr == 0) break;
			nodeIdx = stack[--stackPtr];
		}
		usedBVH4Nodes = usedBVHNodes; // there will be gaps / unused nodes though.
	}
	else if (from == BASIC_BVH4 && to == BVH4_GPU)
	{
		// Convert a 4-wide BVH to a format suitable for GPU traversal. Layout:
		// offs 0:   aabbMin (12 bytes), 4x quantized child xmin (4 bytes)
		// offs 16:  aabbMax (12 bytes), 4x quantized child xmax (4 bytes)
		// offs 32:  4x child ymin, then ymax, zmax, zmax (total 16 bytes)
		// offs 48:  4x child node info: leaf if MSB set.
		//           Leaf: 15 bits for tri count, 16 for offset
		//           Interior: 32 bits for position of child node.
		// Triangle data ('by value') immediately follows each leaf node.
		unsigned blocksNeeded = usedBVH4Nodes * 4; // here, 'block' is 16 bytes.
		blocksNeeded += 6 * triCount; // this layout stores tris in the same buffer.
		if (allocatedAlt4aBlocks < blocksNeeded)
		{
			FATAL_ERROR_IF( bvh4Node == 0, "BVH::Convert( BASIC_BVH4, BVH4_GPU ), bvh4Node == 0." );
			AlignedFree( bvh4Alt );
			bvh4Alt = (bvhvec4*)AlignedAlloc( blocksNeeded * 16 );
			allocatedAlt4aBlocks = blocksNeeded;
		}
		memset( bvh4Alt, 0, 16 * blocksNeeded );
		// start conversion
	#ifdef __GNUC__
	#pragma GCC diagnostic push
	#pragma GCC diagnostic ignored "-Wstrict-aliasing"
	#endif
		unsigned nodeIdx = 0, newAlt4Ptr = 0, stack[128], stackPtr = 0, retValPos = 0;
		while (1)
		{
			const BVHNode4& node = bvh4Node[nodeIdx];
			// convert BVH4 node - must be an interior node.
			assert( !bvh4Node[nodeIdx].isLeaf() );
			bvhvec4* nodeBase = bvh4Alt + newAlt4Ptr;
			unsigned baseAlt4Ptr = newAlt4Ptr;
			newAlt4Ptr += 4;
			nodeBase[0] = bvhvec4( node.aabbMin, 0 );
			nodeBase[1] = bvhvec4( (node.aabbMax - node.aabbMin) * (1.0f / 255.0f), 0 );
			BVHNode4* childNode[4] = {
				&bvh4Node[node.child[0]], &bvh4Node[node.child[1]],
				&bvh4Node[node.child[2]], &bvh4Node[node.child[3]]
			};
			// start with leaf child node conversion
			unsigned childInfo[4] = { 0, 0, 0, 0 }; // will store in final fields later
			for (int i = 0; i < 4; i++) if (childNode[i]->isLeaf())
			{
				childInfo[i] = newAlt4Ptr - baseAlt4Ptr;
				childInfo[i] |= childNode[i]->triCount << 16;
				childInfo[i] |= 0x80000000;
				for (unsigned j = 0; j < childNode[i]->triCount; j++)
				{
					unsigned t = triIdx[childNode[i]->firstTri + j];
					bvhvec4 v0 = verts[t * 3 + 0];
					v0.w = *(float*)&t; // as_float
					bvh4Alt[newAlt4Ptr++] = v0;
					bvh4Alt[newAlt4Ptr++] = verts[t * 3 + 1];
					bvh4Alt[newAlt4Ptr++] = verts[t * 3 + 2];
				}
			}
			// process interior nodes
			for (int i = 0; i < 4; i++) if (!childNode[i]->isLeaf())
			{
				// childInfo[i] = node.child[i] == 0 ? 0 : GPUFormatBVH4( node.child[i] );
				if (node.child[i] == 0) childInfo[i] = 0; else
				{
					stack[stackPtr++] = (unsigned)(((float*)&nodeBase[3] + i) - (float*)bvh4Alt);
					stack[stackPtr++] = node.child[i];
				}
			}
			// store child node bounds, quantized
			const bvhvec3 extent = node.aabbMax - node.aabbMin;
			bvhvec3 scale;
			scale.x = extent.x > 1e-10f ? (254.999f / extent.x) : 0;
			scale.y = extent.y > 1e-10f ? (254.999f / extent.y) : 0;
			scale.z = extent.z > 1e-10f ? (254.999f / extent.z) : 0;
			unsigned char* slot0 = (unsigned char*)&nodeBase[0] + 12;	// 4 chars
			unsigned char* slot1 = (unsigned char*)&nodeBase[1] + 12;	// 4 chars
			unsigned char* slot2 = (unsigned char*)&nodeBase[2];		// 16 chars
			if (node.child[0])
			{
				const bvhvec3 relBMin = childNode[0]->aabbMin - node.aabbMin, relBMax = childNode[0]->aabbMax - node.aabbMin;
				slot0[0] = (unsigned char)floorf( relBMin.x * scale.x ), slot1[0] = (unsigned char)ceilf( relBMax.x * scale.x );
				slot2[0] = (unsigned char)floorf( relBMin.y * scale.y ), slot2[4] = (unsigned char)ceilf( relBMax.y * scale.y );
				slot2[8] = (unsigned char)floorf( relBMin.z * scale.z ), slot2[12] = (unsigned char)ceilf( relBMax.z * scale.z );
			}
			if (node.child[1])
			{
				const bvhvec3 relBMin = childNode[1]->aabbMin - node.aabbMin, relBMax = childNode[1]->aabbMax - node.aabbMin;
				slot0[1] = (unsigned char)floorf( relBMin.x * scale.x ), slot1[1] = (unsigned char)ceilf( relBMax.x * scale.x );
				slot2[1] = (unsigned char)floorf( relBMin.y * scale.y ), slot2[5] = (unsigned char)ceilf( relBMax.y * scale.y );
				slot2[9] = (unsigned char)floorf( relBMin.z * scale.z ), slot2[13] = (unsigned char)ceilf( relBMax.z * scale.z );
			}
			if (node.child[2])
			{
				const bvhvec3 relBMin = childNode[2]->aabbMin - node.aabbMin, relBMax = childNode[2]->aabbMax - node.aabbMin;
				slot0[2] = (unsigned char)floorf( relBMin.x * scale.x ), slot1[2] = (unsigned char)ceilf( relBMax.x * scale.x );
				slot2[2] = (unsigned char)floorf( relBMin.y * scale.y ), slot2[6] = (unsigned char)ceilf( relBMax.y * scale.y );
				slot2[10] = (unsigned char)floorf( relBMin.z * scale.z ), slot2[14] = (unsigned char)ceilf( relBMax.z * scale.z );
			}
			if (node.child[3])
			{
				const bvhvec3 relBMin = childNode[3]->aabbMin - node.aabbMin, relBMax = childNode[3]->aabbMax - node.aabbMin;
				slot0[3] = (unsigned char)floorf( relBMin.x * scale.x ), slot1[3] = (unsigned char)ceilf( relBMax.x * scale.x );
				slot2[3] = (unsigned char)floorf( relBMin.y * scale.y ), slot2[7] = (unsigned char)ceilf( relBMax.y * scale.y );
				slot2[11] = (unsigned char)floorf( relBMin.z * scale.z ), slot2[15] = (unsigned char)ceilf( relBMax.z * scale.z );
			}
			// finalize node
			nodeBase[3] = bvhvec4(
				*(float*)&childInfo[0], *(float*)&childInfo[1],
				*(float*)&childInfo[2], *(float*)&childInfo[3]
			);
			// pop new work from the stack
			if (retValPos > 0) ((unsigned*)bvh4Alt)[retValPos] = baseAlt4Ptr;
			if (stackPtr == 0) break;
			nodeIdx = stack[--stackPtr];
			retValPos = stack[--stackPtr];
		}
	#ifdef __GNUC__
	#pragma GCC diagnostic pop
	#endif
		usedAlt4aBlocks = newAlt4Ptr;
	}
	else if (from == BASIC_BVH4 && to == BVH4_AFRA)
	{
		// Convert a 4-wide BVH to a format suitable for CPU traversal.
		// See Faster Incoherent Ray Traversal Using 8-Wide AVX InstructionsLayout,
		// Atilla T. Áfra, 2013.
		unsigned spaceNeeded = usedBVH4Nodes * 4; // here, 'block' is 16 bytes.
		if (allocatedAlt4bNodes < spaceNeeded)
		{
			FATAL_ERROR_IF( bvh4Node == 0, "BVH::Convert( BASIC_BVH4, BVH4_AFRA ), bvh4Node == 0." );
			AlignedFree( bvh4Alt2 );
			bvh4Alt2 = (BVHNode4Alt2*)AlignedAlloc( spaceNeeded * sizeof( BVHNode4Alt2 ) );
			allocatedAlt4bNodes = spaceNeeded;
		}
		memset( bvh4Alt2, 0, spaceNeeded * sizeof( BVHNode4Alt2 ) );
		// start conversion
		unsigned newAlt4Ptr = 0, nodeIdx = 0, stack[128], stackPtr = 0;
		while (1)
		{
			const BVHNode4& orig = bvh4Node[nodeIdx];
			BVHNode4Alt2& newNode = bvh4Alt2[newAlt4Ptr++];
		#ifdef __GNUC__
		#pragma GCC diagnostic push
		#pragma GCC diagnostic ignored "-Wstrict-aliasing"
		#endif
			for (int cidx = 0, i = 0; i < 4; i++) if (orig.child[i])
			{
				const BVHNode4& child = bvh4Node[orig.child[i]];
				((float*)&newNode.xmin4)[cidx] = child.aabbMin.x, ((float*)&newNode.ymin4)[cidx] = child.aabbMin.y;
				((float*)&newNode.zmin4)[cidx] = child.aabbMin.z, ((float*)&newNode.xmax4)[cidx] = child.aabbMax.x;
				((float*)&newNode.ymax4)[cidx] = child.aabbMax.y, ((float*)&newNode.zmax4)[cidx] = child.aabbMax.z;
				if (child.isLeaf())
					newNode.childFirst[cidx] = orig.firstTri, newNode.triCount[cidx] = orig.triCount;
				else
					stack[stackPtr++] = (unsigned)((float*)&newNode.childFirst[cidx] - (float*)bvh4Alt2),
					stack[stackPtr++] = orig.child[i];
				cidx++;
			}
			// pop next task
			if (!stackPtr) break;
			nodeIdx = stack[--stackPtr], ((unsigned*)bvh4Alt2)[stack[--stackPtr]] = newAlt4Ptr;
		#ifdef __GNUC__
		#pragma GCC diagnostic pop
		#endif
		}
		usedAlt4bNodes = newAlt4Ptr;
	}
	else if (from == WALD_32BYTE && to == BASIC_BVH8)
	{
		// allocate space
		const unsigned spaceNeeded = usedBVHNodes;
		if (allocatedBVH8Nodes < spaceNeeded)
		{
			FATAL_ERROR_IF( bvhNode == 0, "BVH::Convert( WALD_32BYTE, BASIC_BVH8 ), bvhNode == 0." );
			AlignedFree( bvh8Node );
			bvh8Node = (BVHNode8*)AlignedAlloc( spaceNeeded * sizeof( BVHNode8 ) );
			allocatedBVH8Nodes = spaceNeeded;
		}
		memset( bvh8Node, 0, sizeof( BVHNode8 ) * spaceNeeded );
		// create an mbvh node for each bvh2 node
		for (unsigned i = 0; i < usedBVHNodes; i++) if (i != 1)
		{
			BVHNode& orig = bvhNode[i];
			BVHNode8& node8 = bvh8Node[i];
			node8.aabbMin = orig.aabbMin, node8.aabbMax = orig.aabbMax;
			if (orig.isLeaf()) node8.triCount = orig.triCount, node8.firstTri = orig.leftFirst;
			else node8.child[0] = orig.leftFirst, node8.child[1] = orig.leftFirst + 1, node8.childCount = 2;
		}
		// collapse
		unsigned stack[128], stackPtr = 1, nodeIdx = stack[0] = 0; // i.e., root node
		while (1)
		{
			BVHNode8& node = bvh8Node[nodeIdx];
			while (node.childCount < 8)
			{
				int bestChild = -1;
				float bestChildSA = 0;
				for (unsigned i = 0; i < node.childCount; i++)
				{
					// see if we can adopt child i
					const BVHNode8& child = bvh8Node[node.child[i]];
					if ((!child.isLeaf()) && (node.childCount - 1 + child.childCount) <= 8)
					{
						const float childSA = SA( child.aabbMin, child.aabbMax );
						if (childSA > bestChildSA) bestChild = i, bestChildSA = childSA;
					}
				}
				if (bestChild == -1) break; // could not adopt
				const BVHNode8& child = bvh8Node[node.child[bestChild]];
				node.child[bestChild] = child.child[0];
				for (unsigned i = 1; i < child.childCount; i++)
					node.child[node.childCount++] = child.child[i];
			}
			// we're done with the node; proceed with the children
			for (unsigned i = 0; i < node.childCount; i++)
			{
				const unsigned childIdx = node.child[i];
				const BVHNode8& child = bvh8Node[childIdx];
				if (!child.isLeaf()) stack[stackPtr++] = childIdx;
			}
			if (stackPtr == 0) break;
			nodeIdx = stack[--stackPtr];
		}
		usedBVH8Nodes = usedBVHNodes; // there will be gaps / unused nodes though.
	}
	else if (from == BASIC_BVH8 && to == CWBVH)
	{
		// Convert a BVH8 to the format specified in: "Efficient Incoherent Ray
		// Traversal on GPUs Through Compressed Wide BVHs", Ylitie et al. 2017.
		// Adapted from code by "AlanWBFT".
		FATAL_ERROR_IF( bvh8Node[0].isLeaf(), "BVH::Convert( BASIC_BVH8, CWBVH ), collapsing single-node bvh." );
		// allocate memory
		// Note: This can be far lower (specifically: usedBVH8Nodes) if we know that
		// none of the BVH8 leafs has more than three primitives. 
		// Without this guarantee, the only safe upper limit is triCount * 2, since 
		// we will be splitting fat leafs to as we go.
		unsigned spaceNeeded = triCount * 2 * 5; // CWBVH nodes use 80 bytes each.
		if (spaceNeeded > allocatedCWBVHBlocks)
		{
			FATAL_ERROR_IF( bvh8Node == 0, "BVH::Convert( BASIC_BVH8, CWBVH ), bvh8Node == 0." );
			bvh8Compact = (bvhvec4*)AlignedAlloc( spaceNeeded * 16 );
			bvh8Tris = (bvhvec4*)AlignedAlloc( idxCount * 3 * 16 );
			allocatedCWBVHBlocks = spaceNeeded;
		}
		memset( bvh8Compact, 0, spaceNeeded * 16 );
		memset( bvh8Tris, 0, idxCount * 3 * 16 );
		BVHNode8* stackNodePtr[256];
		unsigned stackNodeAddr[256], stackPtr = 1, nodeDataPtr = 5, triDataPtr = 0;
		stackNodePtr[0] = &bvh8Node[0], stackNodeAddr[0] = 0;
		// start conversion
		while (stackPtr > 0)
		{
			BVHNode8* node = stackNodePtr[--stackPtr];
			const int currentNodeAddr = stackNodeAddr[stackPtr];
			bvhvec3 nodeLo = node->aabbMin, nodeHi = node->aabbMax;
			// greedy child node ordering
			const bvhvec3 nodeCentroid = (nodeLo + nodeHi) * 0.5f;
			float cost[8][8];
			int assignment[8];
			bool isSlotEmpty[8];
			for (int s = 0; s < 8; s++)
			{
				isSlotEmpty[s] = true, assignment[s] = -1;
				bvhvec3 ds(
					(((s >> 2) & 1) == 1) ? -1.0f : 1.0f,
					(((s >> 1) & 1) == 1) ? -1.0f : 1.0f,
					(((s >> 0) & 1) == 1) ? -1.0f : 1.0f
				);
				for (int i = 0; i < 8; i++) if (node->child[i] == 0) cost[s][i] = 1e30f; else
				{
					BVHNode8* const child = &bvh8Node[node->child[i]];
					if (child->triCount > 3 /* must be leaf */) SplitBVH8Leaf( node->child[i], 3 );
					bvhvec3 childCentroid = (child->aabbMin + child->aabbMax) * 0.5f;
					cost[s][i] = dot( childCentroid - nodeCentroid, ds );
				}
			}
			while (1)
			{
				float minCost = 1e30f;
				int minEntryx = -1, minEntryy = -1;
				for (int s = 0; s < 8; s++) for (int i = 0; i < 8; i++)
					if (assignment[i] == -1 && isSlotEmpty[s] && cost[s][i] < minCost)
						minCost = cost[s][i], minEntryx = s, minEntryy = i;
				if (minEntryx == -1 && minEntryy == -1) break;
				isSlotEmpty[minEntryx] = false, assignment[minEntryy] = minEntryx;
			}
			for (int i = 0; i < 8; i++) if (assignment[i] == -1) for (int s = 0; s < 8; s++) if (isSlotEmpty[s])
			{
				isSlotEmpty[s] = false, assignment[i] = s;
				break;
			}
			const BVHNode8 oldNode = *node;
			for (int i = 0; i < 8; i++) node->child[assignment[i]] = oldNode.child[i];
			// calculate quantization parameters for each axis
			const int ex = (int)((char)ceilf( log2f( (nodeHi.x - nodeLo.x) / 255.0f ) ));
			const int ey = (int)((char)ceilf( log2f( (nodeHi.y - nodeLo.y) / 255.0f ) ));
			const int ez = (int)((char)ceilf( log2f( (nodeHi.z - nodeLo.z) / 255.0f ) ));
			// encode output
			int internalChildCount = 0, leafChildTriCount = 0, childBaseIndex = 0, triangleBaseIndex = 0;
			unsigned char imask = 0;
		#ifdef __GNUC__
		#pragma GCC diagnostic push
		#pragma GCC diagnostic ignored "-Wstrict-aliasing"
		#endif
			for (int i = 0; i < 8; i++)
			{
				if (node->child[i] == 0) continue;
				BVHNode8* const child = &bvh8Node[node->child[i]];
				const int qlox = (int)floorf( (child->aabbMin.x - nodeLo.x) / powf( 2, (float)ex ) );
				const int qloy = (int)floorf( (child->aabbMin.y - nodeLo.y) / powf( 2, (float)ey ) );
				const int qloz = (int)floorf( (child->aabbMin.z - nodeLo.z) / powf( 2, (float)ez ) );
				const int qhix = (int)ceilf( (child->aabbMax.x - nodeLo.x) / powf( 2, (float)ex ) );
				const int qhiy = (int)ceilf( (child->aabbMax.y - nodeLo.y) / powf( 2, (float)ey ) );
				const int qhiz = (int)ceilf( (child->aabbMax.z - nodeLo.z) / powf( 2, (float)ez ) );
				unsigned char* const baseAddr = (unsigned char*)&bvh8Compact[currentNodeAddr + 2];
				baseAddr[i + 0] = (unsigned char)qlox, baseAddr[i + 24] = (unsigned char)qhix;
				baseAddr[i + 8] = (unsigned char)qloy, baseAddr[i + 32] = (unsigned char)qhiy;
				baseAddr[i + 16] = (unsigned char)qloz, baseAddr[i + 40] = (unsigned char)qhiz;
				if (!child->isLeaf())
				{
					// interior node, set params and push onto stack
					const int childNodeAddr = nodeDataPtr;
					if (internalChildCount++ == 0) childBaseIndex = childNodeAddr / 5;
					nodeDataPtr += 5, imask |= 1 << i;
					// set the meta field - This calculation assumes children are stored contiguously.
					unsigned char* const childMetaField = ((unsigned char*)&bvh8Compact[currentNodeAddr + 1]) + 8;
					childMetaField[i] = (1 << 5) | (24 + (unsigned char)i); // I don't see how this accounts for empty children?
					stackNodePtr[stackPtr] = child, stackNodeAddr[stackPtr++] = childNodeAddr; // counted in float4s
					internalChildCount++;
					continue;
				}
				// leaf node
				const unsigned tcount = tinybvh_min( child->triCount, 3u ); // TODO: ensure that's the case; clamping for now.
				if (leafChildTriCount == 0) triangleBaseIndex = triDataPtr;
				int unaryEncodedTriCount = tcount == 1 ? 0b001 : tcount == 2 ? 0b011 : 0b111;
				// set the meta field - This calculation assumes children are stored contiguously.
				unsigned char* const childMetaField = ((unsigned char*)&bvh8Compact[currentNodeAddr + 1]) + 8;
				childMetaField[i] = (unsigned char)((unaryEncodedTriCount << 5) | leafChildTriCount);
				leafChildTriCount += tcount;
				for (unsigned j = 0; j < tcount; j++)
				{
					int primitiveIndex = triIdx[child->firstTri + j];
					bvhvec4 t = verts[primitiveIndex * 3 + 0];
					t.w = *(float*)&primitiveIndex;
					bvh8Tris[triDataPtr++] = t;
					bvh8Tris[triDataPtr++] = verts[primitiveIndex * 3 + 1];
					bvh8Tris[triDataPtr++] = verts[primitiveIndex * 3 + 2];
				}
			}
			unsigned char exyzAndimask[4] = { *(unsigned char*)&ex, *(unsigned char*)&ey, *(unsigned char*)&ez, imask };
			bvh8Compact[currentNodeAddr + 0] = bvhvec4( nodeLo, *(float*)&exyzAndimask );
			bvh8Compact[currentNodeAddr + 1].x = *(float*)&childBaseIndex;
			bvh8Compact[currentNodeAddr + 1].y = *(float*)&triangleBaseIndex;
		#ifdef __GNUC__
		#pragma GCC diagnostic pop
		#endif
		}
		usedCWBVHBlocks = nodeDataPtr;
	}
	else if (from == VERBOSE && to == WALD_32BYTE)
	{
		// allocate space
		const unsigned spaceNeeded = usedVerboseNodes;
		if (allocatedBVHNodes < spaceNeeded)
		{
			FATAL_ERROR_IF( verbose == 0, "BVH::Convert( VERBOSE, WALD_32BYTE ), verbose == 0." );
			AlignedFree( bvhNode );
			bvhNode = (BVHNode*)AlignedAlloc( triCount * 2 * sizeof( BVHNode ) );
			allocatedBVHNodes = spaceNeeded;
		}
		memset( bvhNode, 0, sizeof( BVHNode ) * spaceNeeded );
		// start conversion
		unsigned srcNodeIdx = 0, dstNodeIdx = 0, newNodePtr = 2;
		unsigned srcStack[64], dstStack[64], stackPtr = 0;
		while (1)
		{
			const BVHNodeVerbose& srcNode = verbose[srcNodeIdx];
			bvhNode[dstNodeIdx].aabbMin = srcNode.aabbMin;
			bvhNode[dstNodeIdx].aabbMax = srcNode.aabbMax;
			if (srcNode.isLeaf())
			{
				bvhNode[dstNodeIdx].triCount = srcNode.triCount;
				bvhNode[dstNodeIdx].leftFirst = srcNode.firstTri;
				if (stackPtr == 0) break;
				srcNodeIdx = srcStack[--stackPtr];
				dstNodeIdx = dstStack[stackPtr];
			}
			else
			{
				bvhNode[dstNodeIdx].leftFirst = newNodePtr;
				unsigned srcRightIdx = srcNode.right;
				srcNodeIdx = srcNode.left, dstNodeIdx = newNodePtr++;
				srcStack[stackPtr] = srcRightIdx;
				dstStack[stackPtr++] = newNodePtr++;
			}
		}
		usedBVHNodes = usedVerboseNodes;
	}
	else
	{
		// For now all other conversions are invalid.
		FATAL_ERROR_IF( true, "BVH::Convert( .. , .. ), unsupported conversion." );
	}
}

int BVH::NodeCount( const BVHLayout layout ) const
{
	// Determine the number of nodes in the tree. Typically the result should
	// be usedBVHNodes - 1 (second node is always unused), but some builders may 
	// have unused nodes besides node 1. TODO: Support more layouts.
	unsigned retVal = 0, nodeIdx = 0, stack[64], stackPtr = 0;
	if (layout == WALD_32BYTE)
	{
		FATAL_ERROR_IF( bvhNode == 0, "BVH::NodeCount( WALD_32BYTE ), bvhNode == 0." );
		while (1)
		{
			const BVHNode& n = bvhNode[nodeIdx];
			retVal++;
			if (n.isLeaf()) { if (stackPtr == 0) break; else nodeIdx = stack[--stackPtr]; }
			else nodeIdx = n.leftFirst, stack[stackPtr++] = n.leftFirst + 1;
		}
	}
	else if (layout == VERBOSE)
	{
		FATAL_ERROR_IF( bvhNode == 0, "BVH::NodeCount( VERBOSE ), verbose == 0." );
		while (1)
		{
			const BVHNodeVerbose& n = verbose[nodeIdx];
			retVal++;
			if (n.isLeaf()) { if (stackPtr == 0) break; else nodeIdx = stack[--stackPtr]; }
			else nodeIdx = n.left, stack[stackPtr++] = n.right;
		}
	}
	return retVal;
}

// Refitting: For animated meshes, where the topology remains intact. This
// includes trees waving in the wind, or subsequent frames for skinned
// animations. Repeated refitting tends to lead to deteriorated BVHs and
// slower ray tracing. Rebuild when this happens.
void BVH::Refit( const BVHLayout layout, const unsigned nodeIdx )
{
	FATAL_ERROR_IF( !refittable, "BVH::Refit( .. ), refitting an SBVH." );
	if (layout == WALD_32BYTE)
	{
		FATAL_ERROR_IF( bvhNode == 0, "BVH::Refit( WALD_32BYTE ), bvhNode == 0." );
		FATAL_ERROR_IF( may_have_holes, "BVH::Refit( WALD_32BYTE ), bvh may have holes." );
		for (int i = usedBVHNodes - 1; i >= 0; i--)
		{
			BVHNode& node = bvhNode[i];
			if (node.isLeaf()) // leaf: adjust to current triangle vertex positions
			{
				bvhvec4 aabbMin( 1e30f ), aabbMax( -1e30f );
				for (unsigned first = node.leftFirst, j = 0; j < node.triCount; j++)
				{
					const unsigned vertIdx = triIdx[first + j] * 3;
					aabbMin = tinybvh_min( aabbMin, verts[vertIdx] ), aabbMax = tinybvh_max( aabbMax, verts[vertIdx] );
					aabbMin = tinybvh_min( aabbMin, verts[vertIdx + 1] ), aabbMax = tinybvh_max( aabbMax, verts[vertIdx + 1] );
					aabbMin = tinybvh_min( aabbMin, verts[vertIdx + 2] ), aabbMax = tinybvh_max( aabbMax, verts[vertIdx + 2] );
				}
				node.aabbMin = aabbMin, node.aabbMax = aabbMax;
				continue;
			}
			// interior node: adjust to child bounds
			const BVHNode& left = bvhNode[node.leftFirst], & right = bvhNode[node.leftFirst + 1];
			node.aabbMin = tinybvh_min( left.aabbMin, right.aabbMin );
			node.aabbMax = tinybvh_max( left.aabbMax, right.aabbMax );
		}
	}
	else if (layout == VERBOSE)
	{
		// use a recursive process for verbose nodes: order not be guaranteed
		FATAL_ERROR_IF( verbose == 0, "BVH::Refit( VERBOSE ), verbose == 0." );
		BVHNodeVerbose& node = verbose[nodeIdx];
		if (node.isLeaf()) // leaf: adjust to current triangle vertex positions
		{
			bvhvec4 aabbMin( 1e30f ), aabbMax( -1e30f );
			for (unsigned first = node.firstTri, j = 0; j < node.triCount; j++)
			{
				const unsigned vertIdx = triIdx[first + j] * 3;
				aabbMin = tinybvh_min( aabbMin, verts[vertIdx] ), aabbMax = tinybvh_max( aabbMax, verts[vertIdx] );
				aabbMin = tinybvh_min( aabbMin, verts[vertIdx + 1] ), aabbMax = tinybvh_max( aabbMax, verts[vertIdx + 1] );
				aabbMin = tinybvh_min( aabbMin, verts[vertIdx + 2] ), aabbMax = tinybvh_max( aabbMax, verts[vertIdx + 2] );
			}
			node.aabbMin = aabbMin, node.aabbMax = aabbMax;
		}
		else
		{
			Refit( VERBOSE, node.left );
			Refit( VERBOSE, node.right );
			node.aabbMin = tinybvh_min( verbose[node.left].aabbMin, verbose[node.right].aabbMin );
			node.aabbMax = tinybvh_max( verbose[node.left].aabbMax, verbose[node.right].aabbMax );
		}
	}
	else
	{
		// not (yet) implemented.
		FATAL_ERROR_IF( true, "BVH::Refit( .. ), unsupported bvh layout." );
	}
}

// Compact: Reduce the size of a BVH by removing any unsed nodes.
// This is useful after an SBVH build or multi-threaded build, but also after
// calling MergeLeafs. Some operations, such as Optimize, *require* a
// compacted tree to work correctly.
void BVH::Compact( const BVHLayout layout )
{
	if (layout == WALD_32BYTE)
	{
		FATAL_ERROR_IF( bvhNode == 0, "BVH::Compact( WALD_32BYTE ), bvhNode == 0." );
		BVHNode* tmp = (BVHNode*)AlignedAlloc( sizeof( BVHNode ) * usedBVHNodes );
		memcpy( tmp, bvhNode, 2 * sizeof( BVHNode ) );
		unsigned newNodePtr = 2, nodeIdx = 0, stack[64], stackPtr = 0;
		while (1)
		{
			BVHNode& node = tmp[nodeIdx];
			const BVHNode& left = bvhNode[node.leftFirst];
			const BVHNode& right = bvhNode[node.leftFirst + 1];
			tmp[newNodePtr] = left, tmp[newNodePtr + 1] = right;
			const unsigned todo1 = newNodePtr, todo2 = newNodePtr + 1;
			node.leftFirst = newNodePtr, newNodePtr += 2;
			if (!left.isLeaf()) stack[stackPtr++] = todo1;
			if (!right.isLeaf()) stack[stackPtr++] = todo2;
			if (!stackPtr) break;
			nodeIdx = stack[--stackPtr];
		}
		usedBVHNodes = newNodePtr;
		AlignedFree( bvhNode );
		bvhNode = tmp;
	}
	else if (layout == VERBOSE)
	{
		// Same algorithm but different data.
		// Note that compacting a 'verbose' BVH has the side effect that
		// left and right children are stored consecutively.
		FATAL_ERROR_IF( verbose == 0, "BVH::Compact( VERBOSE ), verbose == 0." );
		BVHNodeVerbose* tmp = (BVHNodeVerbose*)AlignedAlloc( sizeof( BVHNodeVerbose ) * usedVerboseNodes );
		memcpy( tmp, verbose, 2 * sizeof( BVHNodeVerbose ) );
		unsigned newNodePtr = 2, nodeIdx = 0, stack[64], stackPtr = 0;
		while (1)
		{
			BVHNodeVerbose& node = tmp[nodeIdx];
			const BVHNodeVerbose& left = verbose[node.left];
			const BVHNodeVerbose& right = verbose[node.right];
			tmp[newNodePtr] = left, tmp[newNodePtr + 1] = right;
			const unsigned todo1 = newNodePtr, todo2 = newNodePtr + 1;
			node.left = newNodePtr++, node.right = newNodePtr++;
			if (!left.isLeaf()) stack[stackPtr++] = todo1;
			if (!right.isLeaf()) stack[stackPtr++] = todo2;
			if (!stackPtr) break;
			nodeIdx = stack[--stackPtr];
		}
		usedVerboseNodes = newNodePtr;
		AlignedFree( verbose );
		verbose = tmp;
	}
	else
	{
		FATAL_ERROR_IF( true, "BVH::Compact( .. ), unsupported bvh layout." );
	}
}

// Single-primitive leafs: Prepare the BVH for optimization. While it is not strictly
// necessary to have a single primitive per leaf, it will yield a slightly better
// optimized BVH. The leafs of the optimized BVH should be collapsed ('MergeLeafs')
// to obtain the final tree.
void BVH::SplitLeafs( const unsigned maxPrims )
{
	FATAL_ERROR_IF( verbose == 0, "BVH::SplitLeafs(), requires VERBOSE bvh." );
	unsigned nodeIdx = 0, stack[64], stackPtr = 0;
	float fragMinFix = frag_min_flipped ? -1.0f : 1.0f;
	while (1)
	{
		BVHNodeVerbose& node = verbose[nodeIdx];
		if (!node.isLeaf()) nodeIdx = node.left, stack[stackPtr++] = node.right; else
		{
			// split this leaf
			if (node.triCount > maxPrims)
			{
				const unsigned newIdx1 = usedVerboseNodes++;
				const unsigned newIdx2 = usedVerboseNodes++;
				BVHNodeVerbose& new1 = verbose[newIdx1], & new2 = verbose[newIdx2];
				new1.firstTri = node.firstTri;
				new1.triCount = node.triCount / 2;
				new1.parent = new2.parent = nodeIdx;
				new1.left = new1.right = 0;
				new2.firstTri = node.firstTri + new1.triCount;
				new2.triCount = node.triCount - new1.triCount;
				new2.left = new2.right = 0;
				node.left = newIdx1, node.right = newIdx2, node.triCount = 0;
				new1.aabbMin = new2.aabbMin = 1e30f, new1.aabbMax = new2.aabbMax = -1e30f;
				for (unsigned fi, i = 0; i < new1.triCount; i++)
					fi = triIdx[new1.firstTri + i],
					new1.aabbMin = tinybvh_min( new1.aabbMin, fragment[fi].bmin * fragMinFix ),
					new1.aabbMax = tinybvh_max( new1.aabbMax, fragment[fi].bmax );
				for (unsigned fi, i = 0; i < new2.triCount; i++)
					fi = triIdx[new2.firstTri + i],
					new2.aabbMin = tinybvh_min( new2.aabbMin, fragment[fi].bmin * fragMinFix ),
					new2.aabbMax = tinybvh_max( new2.aabbMax, fragment[fi].bmax );
				// recurse
				if (new1.triCount > 1) stack[stackPtr++] = newIdx1;
				if (new2.triCount > 1) stack[stackPtr++] = newIdx2;
			}
			if (stackPtr == 0) break; else nodeIdx = stack[--stackPtr];
		}
	}
}

// SplitBVH8Leaf: CWBVH requires that a leaf has no more than 3 primitives,
// but regular BVH construction does not guarantee this. So, here we split
// busy leafs recursively in multiple leaves, until the requirement is met.
void BVH::SplitBVH8Leaf( const unsigned nodeIdx, const unsigned maxPrims )
{
	float fragMinFix = frag_min_flipped ? -1.0f : 1.0f;
	BVHNode8& node = bvh8Node[nodeIdx];
	if (node.triCount <= maxPrims) return; // also catches interior nodes
	// place all primitives in a new node and make this the first child of 'node'
	BVHNode8& firstChild = bvh8Node[node.child[0] = usedBVH8Nodes++];
	firstChild.triCount = node.triCount;
	firstChild.firstTri = node.firstTri;
	unsigned nextChild = 1;
	// share with new sibling nodes
	while (firstChild.triCount > maxPrims && nextChild < 8)
	{
		BVHNode8& child = bvh8Node[node.child[nextChild] = usedBVH8Nodes++];
		firstChild.triCount -= maxPrims, child.triCount = maxPrims;
		child.firstTri = firstChild.firstTri + firstChild.triCount;
	}
	for (unsigned i = 0; i < nextChild; i++)
	{
		BVHNode8& child = bvh8Node[node.child[i]];
		child.aabbMin = bvhvec3( 1e30f ), child.aabbMax = bvhvec3( -1e30f );
		for (unsigned j = 0; j < child.triCount; j++)
		{
			unsigned fi = triIdx[child.firstTri + i];
			child.aabbMin = tinybvh_min( child.aabbMin, fragment[fi].bmin * fragMinFix );
			child.aabbMax = tinybvh_max( child.aabbMax, fragment[fi].bmax );
		}
	}
	node.triCount = 0;
	// recurse; should be rare
	if (firstChild.triCount > maxPrims) SplitBVH8Leaf( node.child[0], maxPrims );
}

// MergeLeafs: After optimizing a BVH, single-primitive leafs should be merged whenever
// SAH indicates this is an improvement.
void BVH::MergeLeafs()
{
	// allocate some working space
	FATAL_ERROR_IF( verbose == 0, "BVH::MergeLeafs(), requires VERBOSE bvh." );
	unsigned* subtreeTriCount = (unsigned*)AlignedAlloc( usedVerboseNodes * 4 );
	unsigned* newIdx = (unsigned*)AlignedAlloc( idxCount * 4 );
	memset( subtreeTriCount, 0, usedVerboseNodes * 4 );
	CountSubtreeTris( 0, subtreeTriCount );
	unsigned stack[64], stackPtr = 0, nodeIdx = 0, newIdxPtr = 0;
	while (1)
	{
		BVHNodeVerbose& node = verbose[nodeIdx];
		if (node.isLeaf())
		{
			unsigned start = newIdxPtr;
			MergeSubtree( nodeIdx, newIdx, newIdxPtr );
			node.firstTri = start;
			// pop new task
			if (stackPtr == 0) break;
			nodeIdx = stack[--stackPtr];
		}
		else
		{
			const unsigned leftCount = subtreeTriCount[node.left];
			const unsigned rightCount = subtreeTriCount[node.right];
			const unsigned mergedCount = leftCount + rightCount;
			// cost of unsplit
			float Cunsplit = SA( node.aabbMin, node.aabbMax ) * mergedCount * C_INT;
			// cost of leaving things as they are
			BVHNodeVerbose& left = verbose[node.left];
			BVHNodeVerbose& right = verbose[node.right];
			float Ckeepsplit =
				C_TRAV + C_INT *
				(SA( left.aabbMin, left.aabbMax ) * leftCount +
					SA( right.aabbMin, right.aabbMax ) * rightCount);
			if (Cunsplit <= Ckeepsplit)
			{
				// collapse the subtree
				unsigned start = newIdxPtr;
				MergeSubtree( nodeIdx, newIdx, newIdxPtr );
				node.firstTri = start;
				node.triCount = mergedCount;
				node.left = node.right = 0;
				// pop new task
				if (stackPtr == 0) break;
				nodeIdx = stack[--stackPtr];
			}
			else
			{
				// recurse
				nodeIdx = node.left;
				stack[stackPtr++] = node.right;
			}
		}
	}
	// cleanup
	AlignedFree( subtreeTriCount );
	AlignedFree( triIdx );
	triIdx = newIdx;
	may_have_holes = true; // all over the place, in fact
}

// Determine for each node in the tree the number of primitives
// stored in that subtree. Helper function for MergeLeafs.
unsigned BVH::CountSubtreeTris( const unsigned nodeIdx, unsigned* counters )
{
	BVHNodeVerbose& node = verbose[nodeIdx];
	unsigned result = node.triCount;
	if (!result)
		result = CountSubtreeTris( node.left, counters ) + CountSubtreeTris( node.right, counters );
	counters[nodeIdx] = result;
	return result;
}

// Write the triangle indices stored in a subtree to a continuous
// slice in the 'newIdx' array. Helper function for MergeLeafs.
void BVH::MergeSubtree( const unsigned nodeIdx, unsigned* newIdx, unsigned& newIdxPtr )
{
	BVHNodeVerbose& node = verbose[nodeIdx];
	if (node.isLeaf())
	{
		memcpy( newIdx + newIdxPtr, triIdx + node.firstTri, node.triCount * 4 );
		newIdxPtr += node.triCount;
	}
	else
	{
		MergeSubtree( node.left, newIdx, newIdxPtr );
		MergeSubtree( node.right, newIdx, newIdxPtr );
	}
}

// Optimizing a BVH: BVH must be in 'verbose' format.
// Implements "Fast Insertion-Based Optimization of Bounding Volume Hierarchies",
void BVH::Optimize( const unsigned iterations, const bool convertBack )
{
	// Optimize by reinserting a random subtree.
	// Suggested iteration count: ~1M for best results.
	// TODO: Implement Section 3.4 of the paper to speed up the process.
	FATAL_ERROR_IF( verbose == 0, "BVH::Optimize(), requires VERBOSE bvh." );
	for (unsigned i = 0; i < iterations; i++)
	{
		unsigned Nid, valid = 0;
		do
		{
			static unsigned seed = 0x12345678;
			seed ^= seed << 13, seed ^= seed >> 17, seed ^= seed << 5; // xor32
			valid = 1, Nid = 2 + seed % (usedVerboseNodes - 2);
			if (verbose[Nid].parent == 0 || verbose[Nid].isLeaf()) valid = 0;
			if (valid) if (verbose[verbose[Nid].parent].parent == 0) valid = 0;
		} while (valid == 0);
		// snip it loose
		const BVHNodeVerbose& N = verbose[Nid], & P = verbose[N.parent];
		const unsigned Pid = N.parent, X1 = P.parent;
		const unsigned X2 = P.left == Nid ? P.right : P.left;
		if (verbose[X1].left == Pid) verbose[X1].left = X2;
		else /* verbose[X1].right == Pid */ verbose[X1].right = X2;
		verbose[X2].parent = X1;
		unsigned L = N.left, R = N.right;
		// fix affected node bounds
		RefitUpVerbose( X1 );
		ReinsertNodeVerbose( L, Pid, X1 );
		ReinsertNodeVerbose( R, Nid, X1 );
	}
}

// RefitUpVerbose: Update bounding boxes of ancestors of the specified node.
void BVH::RefitUpVerbose( unsigned nodeIdx )
{
	while (nodeIdx != 0xffffffff)
	{
		BVHNodeVerbose& node = verbose[nodeIdx];
		BVHNodeVerbose& left = verbose[node.left];
		BVHNodeVerbose& right = verbose[node.right];
		node.aabbMin = tinybvh_min( left.aabbMin, right.aabbMin );
		node.aabbMax = tinybvh_max( left.aabbMax, right.aabbMax );
		nodeIdx = node.parent;
	}
}

// FindBestNewPosition
// Part of "Fast Insertion-Based Optimization of Bounding Volume Hierarchies"
unsigned BVH::FindBestNewPosition( const unsigned Lid )
{
	const BVHNodeVerbose& L = verbose[Lid];
	const float SA_L = SA( L.aabbMin, L.aabbMax );
	// reinsert L into BVH
	unsigned taskNode[512], tasks = 1, Xbest = 0;
	float taskCi[512], taskInvCi[512], Cbest = 1e30f, epsilon = 1e-10f;
	taskNode[0] = 0 /* root */, taskCi[0] = 0, taskInvCi[0] = 1 / epsilon;
	while (tasks > 0)
	{
		// 'pop' task with createst taskInvCi
		float maxInvCi = 0;
		unsigned bestTask = 0;
		for (unsigned j = 0; j < tasks; j++) if (taskInvCi[j] > maxInvCi) maxInvCi = taskInvCi[j], bestTask = j;
		const unsigned Xid = taskNode[bestTask];
		const float CiLX = taskCi[bestTask];
		taskNode[bestTask] = taskNode[--tasks], taskCi[bestTask] = taskCi[tasks], taskInvCi[bestTask] = taskInvCi[tasks];
		// execute task
		const BVHNodeVerbose& X = verbose[Xid];
		if (CiLX + SA_L >= Cbest) break;
		const float CdLX = SA( tinybvh_min( L.aabbMin, X.aabbMin ), tinybvh_max( L.aabbMax, X.aabbMax ) );
		const float CLX = CiLX + CdLX;
		if (CLX < Cbest) Cbest = CLX, Xbest = Xid;
		const float Ci = CLX - SA( X.aabbMin, X.aabbMax );
		if (Ci + SA_L < Cbest) if (!X.isLeaf())
		{
			taskNode[tasks] = X.left, taskCi[tasks] = Ci, taskInvCi[tasks++] = 1.0f / (Ci + epsilon);
			taskNode[tasks] = X.right, taskCi[tasks] = Ci, taskInvCi[tasks++] = 1.0f / (Ci + epsilon);
		}
	}
	return Xbest;
}

// ReinsertNodeVerbose
// Part of "Fast Insertion-Based Optimization of Bounding Volume Hierarchies"
void BVH::ReinsertNodeVerbose( const unsigned Lid, const unsigned Nid, const unsigned origin )
{
	unsigned Xbest = FindBestNewPosition( Lid );
	if (Xbest == 0 || verbose[Xbest].parent == 0) Xbest = origin;
	const unsigned X1 = verbose[Xbest].parent;
	BVHNodeVerbose& N = verbose[Nid];
	N.left = Xbest, N.right = Lid;
	N.aabbMin = tinybvh_min( verbose[Xbest].aabbMin, verbose[Lid].aabbMin );
	N.aabbMax = tinybvh_max( verbose[Xbest].aabbMax, verbose[Lid].aabbMax );
	verbose[Nid].parent = X1;
	if (verbose[X1].left == Xbest) verbose[X1].left = Nid; else verbose[X1].right = Nid;
	verbose[Xbest].parent = verbose[Lid].parent = Nid;
	RefitUpVerbose( Nid );
}

// Intersect a BVH with a ray.
// This function returns the intersection details in Ray::hit. Additionally,
// the number of steps through the BVH is returned. Visualize this to get a
// visual impression of the structure of the BVH.
int BVH::Intersect( Ray& ray, const BVHLayout layout ) const
{
	switch (layout)
	{
	case WALD_32BYTE:
		FATAL_ERROR_IF( bvhNode == 0, "BVH::Intersect( .. , WALD_32BYTE ), bvh not available." );
		return Intersect_Wald32Byte( ray );
		break;
	case AILA_LAINE:
		FATAL_ERROR_IF( altNode == 0, "BVH::Intersect( .. , AILA_LAINE ), bvh not available." );
		return Intersect_AilaLaine( ray );
		break;
	case BASIC_BVH4:
		FATAL_ERROR_IF( bvh4Node == 0, "BVH::Intersect( .. , BASIC_BVH4 ), bvh not available." );
		return Intersect_BasicBVH4( ray );
		break;
	case BVH4_GPU:
		FATAL_ERROR_IF( bvh4Alt == 0, "BVH::Intersect( .. , BVH4_GPU ), bvh not available." );
		return Intersect_Alt4BVH( ray );
		break;
	case BASIC_BVH8:
		FATAL_ERROR_IF( bvh8Node == 0, "BVH::Intersect( .. , BASIC_BVH8 ), bvh not available." );
		return Intersect_BasicBVH8( ray );
		break;
	#if defined BVH_USEAVX || defined BVH_USENEON
	case ALT_SOA:
		FATAL_ERROR_IF( alt2Node == 0, "BVH::Intersect( .. , ALT_SOA ), bvh not available." );
		return Intersect_AltSoA( ray );
		break;
	#endif
	#if defined BVH_USEAVX
	case CWBVH:
		FATAL_ERROR_IF( bvh8Compact == 0, "BVH::Intersect( .. , CWBVH ), bvh not available." );
		return Intersect_CWBVH( ray );
		break;
	#endif
	default:
		FATAL_ERROR_IF( true, "BVH::Intersect( .. , ? ), unsupported bvh layout." );
		break;
	}
	return 0;
}

void BVH::BatchIntersect( Ray* rayBatch, const unsigned N, const BVHLayout layout, const TraceDevice device ) const
{
	for (unsigned i = 0; i < N; i++) Intersect( rayBatch[i], layout );
}

// Detect if a ray is occluded / shadow ray query.
// Unlike Intersect, this function only returns a yes/no answer: Yes if any 
// geometry blocks it (taking into account ray length); no if the ray can 
// travel the specified distance without encountering anything. 
bool BVH::IsOccluded( const Ray& ray, const BVHLayout layout ) const
{
	switch (layout)
	{
	case WALD_32BYTE:
		return IsOccluded_Wald32Byte( ray );
		break;
	default:
		// For now, implemented using Intersect, so we have the interface in
		// place. TODO: make specialized code, which will be faster.
	{
		Ray tmp = ray;
		float rayLength = ray.hit.t;
		Intersect( tmp );
		return tmp.hit.t < rayLength;
	}
	break;
	}
}

// Intersect a buffer of rays with the scene.
// For now this exists only to establish the interface.
// A future implementation will exploit the batch to trace the rays faster.
// BatchIsOccluded returns the hits as a bit array in result:
// Each unsigned integer in this array stores 32 hits.
void BVH::BatchIsOccluded( Ray* rayBatch, const unsigned N, unsigned* result, const BVHLayout layout, const TraceDevice device ) const
{
	unsigned words = (N + 31 /* round up */) / 32;
	memset( result, 0, words * 4 );
	for (unsigned i = 0; i < N; i++) if (IsOccluded( rayBatch[i], layout )) result[i >> 5] |= 1 << (i & 31);
}

// Traverse the default BVH layout (WALD_32BYTE).
int BVH::Intersect_Wald32Byte( Ray& ray ) const
{
	BVHNode* node = &bvhNode[0], * stack[64];
	unsigned stackPtr = 0, steps = 0;
	while (1)
	{
		steps++;
		if (node->isLeaf())
		{
			for (unsigned i = 0; i < node->triCount; i++) IntersectTri( ray, triIdx[node->leftFirst + i] );
			if (stackPtr == 0) break; else node = stack[--stackPtr];
			continue;
		}
		BVHNode* child1 = &bvhNode[node->leftFirst];
		BVHNode* child2 = &bvhNode[node->leftFirst + 1];
		float dist1 = child1->Intersect( ray ), dist2 = child2->Intersect( ray );
		if (dist1 > dist2) { tinybvh_swap( dist1, dist2 ); tinybvh_swap( child1, child2 ); }
		if (dist1 == 1e30f /* missed both child nodes */)
		{
			if (stackPtr == 0) break; else node = stack[--stackPtr];
		}
		else /* hit at least one node */
		{
			node = child1; /* continue with the nearest */
			if (dist2 != 1e30f) stack[stackPtr++] = child2; /* push far child */
		}
	}
	return steps;
}

bool BVH::IsOccluded_Wald32Byte( const Ray& ray ) const
{
	BVHNode* node = &bvhNode[0], * stack[64];
	unsigned stackPtr = 0;
	while (1)
	{
		if (node->isLeaf())
		{
			for (unsigned i = 0; i < node->triCount; i++)
			{
				// Moeller-Trumbore ray/triangle intersection algorithm
				const unsigned vertIdx = triIdx[node->leftFirst + i] * 3;
				const bvhvec3 edge1 = verts[vertIdx + 1] - verts[vertIdx];
				const bvhvec3 edge2 = verts[vertIdx + 2] - verts[vertIdx];
				const bvhvec3 h = cross( ray.D, edge2 );
				const float a = dot( edge1, h );
				if (fabs( a ) < 0.0000001f) continue; // ray parallel to triangle
				const float f = 1 / a;
				const bvhvec3 s = ray.O - bvhvec3( verts[vertIdx] );
				const float u = f * dot( s, h );
				if (u < 0 || u > 1) continue;
				const bvhvec3 q = cross( s, edge1 );
				const float v = f * dot( ray.D, q );
				if (v < 0 || u + v > 1) continue;
				const float t = f * dot( edge2, q );
				if (t > 0 && t < ray.hit.t) return true; // no need to look further
			}
			if (stackPtr == 0) break; else node = stack[--stackPtr];
			continue;
		}
		BVHNode* child1 = &bvhNode[node->leftFirst];
		BVHNode* child2 = &bvhNode[node->leftFirst + 1];
		float dist1 = child1->Intersect( ray ), dist2 = child2->Intersect( ray );
		if (dist1 > dist2) { tinybvh_swap( dist1, dist2 ); tinybvh_swap( child1, child2 ); }
		if (dist1 == 1e30f /* missed both child nodes */)
		{
			if (stackPtr == 0) break; else node = stack[--stackPtr];
		}
		else /* hit at least one node */
		{
			node = child1; /* continue with the nearest */
			if (dist2 != 1e30f) stack[stackPtr++] = child2; /* push far child */
		}
	}
	return false;
}

// Traverse the alternative BVH layout (AILA_LAINE).
int BVH::Intersect_AilaLaine( Ray& ray ) const
{
	BVHNodeAlt* node = &altNode[0], * stack[64];
	unsigned stackPtr = 0, steps = 0;
	while (1)
	{
		steps++;
		if (node->isLeaf())
		{
			for (unsigned i = 0; i < node->triCount; i++) IntersectTri( ray, triIdx[node->firstTri + i] );
			if (stackPtr == 0) break; else node = stack[--stackPtr];
			continue;
		}
		const bvhvec3 lmin = node->lmin - ray.O, lmax = node->lmax - ray.O;
		const bvhvec3 rmin = node->rmin - ray.O, rmax = node->rmax - ray.O;
		float dist1 = 1e30f, dist2 = 1e30f;
		const bvhvec3 t1a = lmin * ray.rD, t2a = lmax * ray.rD;
		const bvhvec3 t1b = rmin * ray.rD, t2b = rmax * ray.rD;
		const float tmina = tinybvh_max( tinybvh_max( tinybvh_min( t1a.x, t2a.x ), tinybvh_min( t1a.y, t2a.y ) ), tinybvh_min( t1a.z, t2a.z ) );
		const float tmaxa = tinybvh_min( tinybvh_min( tinybvh_max( t1a.x, t2a.x ), tinybvh_max( t1a.y, t2a.y ) ), tinybvh_max( t1a.z, t2a.z ) );
		const float tminb = tinybvh_max( tinybvh_max( tinybvh_min( t1b.x, t2b.x ), tinybvh_min( t1b.y, t2b.y ) ), tinybvh_min( t1b.z, t2b.z ) );
		const float tmaxb = tinybvh_min( tinybvh_min( tinybvh_max( t1b.x, t2b.x ), tinybvh_max( t1b.y, t2b.y ) ), tinybvh_max( t1b.z, t2b.z ) );
		if (tmaxa >= tmina && tmina < ray.hit.t && tmaxa >= 0) dist1 = tmina;
		if (tmaxb >= tminb && tminb < ray.hit.t && tmaxb >= 0) dist2 = tminb;
		unsigned lidx = node->left, ridx = node->right;
		if (dist1 > dist2)
		{
			float t = dist1; dist1 = dist2; dist2 = t;
			unsigned i = lidx; lidx = ridx; ridx = i;
		}
		if (dist1 == 1e30f)
		{
			if (stackPtr == 0) break; else node = stack[--stackPtr];
		}
		else
		{
			node = altNode + lidx;
			if (dist2 != 1e30f) stack[stackPtr++] = altNode + ridx;
		}
	}
	return steps;
}

// Intersect_BasicBVH4. For testing the converted data only; not efficient.
int BVH::Intersect_BasicBVH4( Ray& ray ) const
{
	BVHNode4* node = &bvh4Node[0], * stack[64];
	unsigned stackPtr = 0, steps = 0;
	while (1)
	{
		steps++;
		if (node->isLeaf()) for (unsigned i = 0; i < node->triCount; i++)
			IntersectTri( ray, triIdx[node->firstTri + i] );
		else for (unsigned i = 0; i < node->childCount; i++)
		{
			BVHNode4* child = bvh4Node + node->child[i];
			float dist = IntersectAABB( ray, child->aabbMin, child->aabbMax );
			if (dist < 1e30f) stack[stackPtr++] = child;
		}
		if (stackPtr == 0) break; else node = stack[--stackPtr];
	}
	return steps;
}

// Intersect_BasicBVH8. For testing the converted data only; not efficient.
int BVH::Intersect_BasicBVH8( Ray& ray ) const
{
	BVHNode8* node = &bvh8Node[0], * stack[512];
	unsigned stackPtr = 0, steps = 0;
	while (1)
	{
		steps++;
		if (node->isLeaf()) for (unsigned i = 0; i < node->triCount; i++)
			IntersectTri( ray, triIdx[node->firstTri + i] );
		else for (unsigned i = 0; i < 8; i++) if (node->child[i])
		{
			BVHNode8* child = bvh8Node + node->child[i];
			float dist = IntersectAABB( ray, child->aabbMin, child->aabbMax );
			if (dist < 1e30f) stack[stackPtr++] = child;
		}
		if (stackPtr == 0) break; else node = stack[--stackPtr];
	}
	return steps;
}

// IntersectAlt4Nodes. For testing the converted data only; not efficient.
// This code replicates how traversal on GPU happens.
#define SWAP(A,B,C,D) t=A,A=B,B=t,t2=C,C=D,D=t2;
struct uchar4 { unsigned char x, y, z, w; };
static uchar4 as_uchar4( const float v ) { union { float t; uchar4 t4; }; t = v; return t4; }
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstrict-aliasing"
#endif
static unsigned as_uint( const float v ) { return *(unsigned*)&v; }
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
int BVH::Intersect_Alt4BVH( Ray& ray ) const
{
	// traverse a blas
	unsigned offset = 0, stack[128], stackPtr = 0, t2 /* for SWAP macro */;
	unsigned steps = 0;
	while (1)
	{
		steps++;
		// fetch the node
		const bvhvec4 data0 = bvh4Alt[offset + 0], data1 = bvh4Alt[offset + 1];
		const bvhvec4 data2 = bvh4Alt[offset + 2], data3 = bvh4Alt[offset + 3];
		// extract aabb
		const bvhvec3 bmin = data0, extent = data1; // pre-scaled by 1/255
		// reconstruct conservative child aabbs
		const uchar4 d0 = as_uchar4( data0.w ), d1 = as_uchar4( data1.w ), d2 = as_uchar4( data2.x );
		const uchar4 d3 = as_uchar4( data2.y ), d4 = as_uchar4( data2.z ), d5 = as_uchar4( data2.w );
		const bvhvec3 c0min = bmin + extent * bvhvec3( d0.x, d2.x, d4.x ), c0max = bmin + extent * bvhvec3( d1.x, d3.x, d5.x );
		const bvhvec3 c1min = bmin + extent * bvhvec3( d0.y, d2.y, d4.y ), c1max = bmin + extent * bvhvec3( d1.y, d3.y, d5.y );
		const bvhvec3 c2min = bmin + extent * bvhvec3( d0.z, d2.z, d4.z ), c2max = bmin + extent * bvhvec3( d1.z, d3.z, d5.z );
		const bvhvec3 c3min = bmin + extent * bvhvec3( d0.w, d2.w, d4.w ), c3max = bmin + extent * bvhvec3( d1.w, d3.w, d5.w );
		// intersect child aabbs
		const bvhvec3 t1a = (c0min - ray.O) * ray.rD, t2a = (c0max - ray.O) * ray.rD;
		const bvhvec3 t1b = (c1min - ray.O) * ray.rD, t2b = (c1max - ray.O) * ray.rD;
		const bvhvec3 t1c = (c2min - ray.O) * ray.rD, t2c = (c2max - ray.O) * ray.rD;
		const bvhvec3 t1d = (c3min - ray.O) * ray.rD, t2d = (c3max - ray.O) * ray.rD;
		const bvhvec3 minta = tinybvh_min( t1a, t2a ), maxta = tinybvh_max( t1a, t2a );
		const bvhvec3 mintb = tinybvh_min( t1b, t2b ), maxtb = tinybvh_max( t1b, t2b );
		const bvhvec3 mintc = tinybvh_min( t1c, t2c ), maxtc = tinybvh_max( t1c, t2c );
		const bvhvec3 mintd = tinybvh_min( t1d, t2d ), maxtd = tinybvh_max( t1d, t2d );
		const float tmina = tinybvh_max( tinybvh_max( tinybvh_max( minta.x, minta.y ), minta.z ), 0.0f );
		const float tminb = tinybvh_max( tinybvh_max( tinybvh_max( mintb.x, mintb.y ), mintb.z ), 0.0f );
		const float tminc = tinybvh_max( tinybvh_max( tinybvh_max( mintc.x, mintc.y ), mintc.z ), 0.0f );
		const float tmind = tinybvh_max( tinybvh_max( tinybvh_max( mintd.x, mintd.y ), mintd.z ), 0.0f );
		const float tmaxa = tinybvh_min( tinybvh_min( tinybvh_min( maxta.x, maxta.y ), maxta.z ), ray.hit.t );
		const float tmaxb = tinybvh_min( tinybvh_min( tinybvh_min( maxtb.x, maxtb.y ), maxtb.z ), ray.hit.t );
		const float tmaxc = tinybvh_min( tinybvh_min( tinybvh_min( maxtc.x, maxtc.y ), maxtc.z ), ray.hit.t );
		const float tmaxd = tinybvh_min( tinybvh_min( tinybvh_min( maxtd.x, maxtd.y ), maxtd.z ), ray.hit.t );
		float dist0 = tmina > tmaxa ? 1e30f : tmina, dist1 = tminb > tmaxb ? 1e30f : tminb;
		float dist2 = tminc > tmaxc ? 1e30f : tminc, dist3 = tmind > tmaxd ? 1e30f : tmind, t;
		// get child node info fields
		unsigned c0info = as_uint( data3.x ), c1info = as_uint( data3.y );
		unsigned c2info = as_uint( data3.z ), c3info = as_uint( data3.w );
		if (dist0 < dist2) SWAP( dist0, dist2, c0info, c2info );
		if (dist1 < dist3) SWAP( dist1, dist3, c1info, c3info );
		if (dist0 < dist1) SWAP( dist0, dist1, c0info, c1info );
		if (dist2 < dist3) SWAP( dist2, dist3, c2info, c3info );
		if (dist1 < dist2) SWAP( dist1, dist2, c1info, c2info );
		// process results, starting with farthest child, so nearest ends on top of stack
		unsigned nextNode = 0;
		unsigned leaf[4] = { 0, 0, 0, 0 }, leafs = 0;
		if (dist0 < 1e30f)
		{
			if (c0info & 0x80000000) leaf[leafs++] = c0info; else if (c0info) stack[stackPtr++] = c0info;
		}
		if (dist1 < 1e30f)
		{
			if (c1info & 0x80000000) leaf[leafs++] = c1info; else if (c1info) stack[stackPtr++] = c1info;
		}
		if (dist2 < 1e30f)
		{
			if (c2info & 0x80000000) leaf[leafs++] = c2info; else if (c2info) stack[stackPtr++] = c2info;
		}
		if (dist3 < 1e30f)
		{
			if (c3info & 0x80000000) leaf[leafs++] = c3info; else if (c3info) stack[stackPtr++] = c3info;
		}
		// process encountered leafs, if any
		for (unsigned i = 0; i < leafs; i++)
		{
			const unsigned N = (leaf[i] >> 16) & 0x7fff;
			unsigned triStart = offset + (leaf[i] & 0xffff);
			for (unsigned j = 0; j < N; j++, triStart += 3)
			{
				const bvhvec3 v0 = bvh4Alt[triStart + 0];
				const bvhvec3 edge1 = bvhvec3( bvh4Alt[triStart + 1] ) - v0;
				const bvhvec3 edge2 = bvhvec3( bvh4Alt[triStart + 2] ) - v0;
				const bvhvec3 h = cross( ray.D, edge2 );
				const float a = dot( edge1, h );
				if (fabs( a ) < 0.0000001f) continue;
				const float f = 1 / a;
				const bvhvec3 s = ray.O - v0;
				const float u = f * dot( s, h );
				if (u < 0 || u > 1) continue;
				const bvhvec3 q = cross( s, edge1 );
				const float v = f * dot( ray.D, q );
				if (v < 0 || u + v > 1) continue;
				const float d = f * dot( edge2, q );
				if (d <= 0.0f || d >= ray.hit.t /* i.e., t */) continue;
				ray.hit.t = d, ray.hit.u = u, ray.hit.v = v;
				ray.hit.prim = as_uint( bvh4Alt[triStart + 0].w );
			}
		}
		// continue with nearest node or first node on the stack
		if (nextNode) offset = nextNode; else
		{
			if (!stackPtr) break;
			offset = stack[--stackPtr];
		}
	}
	return steps;
}

// Intersect a WALD_32BYTE BVH with a ray packet.
// The 256 rays travel together to better utilize the caches and to amortize the cost 
// of memory transfers over the rays in the bundle.
// Note that this basic implementation assumes a specific layout of the rays. Provided
// as 'proof of concept', should not be used in production code.
// Based on Large Ray Packets for Real-time Whitted Ray Tracing, Overbeck et al., 2008,
// extended with sorted traversal and reduced stack traffic.
void BVH::Intersect256Rays( Ray* packet ) const
{
	// convenience macro
#define CALC_TMIN_TMAX_WITH_SLABTEST_ON_RAY( r ) const bvhvec3 rD = packet[r].rD, t1 = o1 * rD, t2 = o2 * rD; \
	const float tmin = tinybvh_max( tinybvh_max( tinybvh_min( t1.x, t2.x ), tinybvh_min( t1.y, t2.y ) ), tinybvh_min( t1.z, t2.z ) ); \
	const float tmax = tinybvh_min( tinybvh_min( tinybvh_max( t1.x, t2.x ), tinybvh_max( t1.y, t2.y ) ), tinybvh_max( t1.z, t2.z ) );
	// Corner rays are: 0, 51, 204 and 255
	// Construct the bounding planes, with normals pointing outwards
	const bvhvec3 O = packet[0].O; // same for all rays in this case
	const bvhvec3 p0 = packet[0].O + packet[0].D; // top-left
	const bvhvec3 p1 = packet[51].O + packet[51].D; // top-right
	const bvhvec3 p2 = packet[204].O + packet[204].D; // bottom-left
	const bvhvec3 p3 = packet[255].O + packet[255].D; // bottom-right
	const bvhvec3 plane0 = normalize( cross( p0 - O, p0 - p2 ) ); // left plane
	const bvhvec3 plane1 = normalize( cross( p3 - O, p3 - p1 ) ); // right plane
	const bvhvec3 plane2 = normalize( cross( p1 - O, p1 - p0 ) ); // top plane
	const bvhvec3 plane3 = normalize( cross( p2 - O, p2 - p3 ) ); // bottom plane
	const int sign0x = plane0.x < 0 ? 4 : 0, sign0y = plane0.y < 0 ? 5 : 1, sign0z = plane0.z < 0 ? 6 : 2;
	const int sign1x = plane1.x < 0 ? 4 : 0, sign1y = plane1.y < 0 ? 5 : 1, sign1z = plane1.z < 0 ? 6 : 2;
	const int sign2x = plane2.x < 0 ? 4 : 0, sign2y = plane2.y < 0 ? 5 : 1, sign2z = plane2.z < 0 ? 6 : 2;
	const int sign3x = plane3.x < 0 ? 4 : 0, sign3y = plane3.y < 0 ? 5 : 1, sign3z = plane3.z < 0 ? 6 : 2;
	const float d0 = dot( O, plane0 ), d1 = dot( O, plane1 );
	const float d2 = dot( O, plane2 ), d3 = dot( O, plane3 );
	// Traverse the tree with the packet
	int first = 0, last = 255; // first and last active ray in the packet
	const BVHNode* node = &bvhNode[0];
	ALIGNED( 64 ) unsigned stack[64], stackPtr = 0;
	while (1)
	{
		if (node->isLeaf())
		{
			// handle leaf node
			for (unsigned j = 0; j < node->triCount; j++)
			{
				const unsigned idx = triIdx[node->leftFirst + j], vid = idx * 3;
				const bvhvec3 edge1 = verts[vid + 1] - verts[vid], edge2 = verts[vid + 2] - verts[vid];
				const bvhvec3 s = O - bvhvec3( verts[vid] );
				for (int i = first; i <= last; i++)
				{
					Ray& ray = packet[i];
					const bvhvec3 h = cross( ray.D, edge2 );
					const float a = dot( edge1, h );
					if (fabs( a ) < 0.0000001f) continue; // ray parallel to triangle
					const float f = 1 / a, u = f * dot( s, h );
					if (u < 0 || u > 1) continue;
					const bvhvec3 q = cross( s, edge1 );
					const float v = f * dot( ray.D, q );
					if (v < 0 || u + v > 1) continue;
					const float t = f * dot( edge2, q );
					if (t <= 0 || t >= ray.hit.t) continue;
					ray.hit.t = t, ray.hit.u = u, ray.hit.v = v, ray.hit.prim = idx;
				}
			}
			if (stackPtr == 0) break; else // pop
				last = stack[--stackPtr], node = bvhNode + stack[--stackPtr],
				first = last >> 8, last &= 255;
		}
		else
		{
			// fetch pointers to child nodes
			const BVHNode* left = bvhNode + node->leftFirst;
			const BVHNode* right = bvhNode + node->leftFirst + 1;
			bool visitLeft = true, visitRight = true;
			int leftFirst = first, leftLast = last, rightFirst = first, rightLast = last;
			float distLeft, distRight;
			{
				// see if we want to intersect the left child
				const bvhvec3 o1( left->aabbMin.x - O.x, left->aabbMin.y - O.y, left->aabbMin.z - O.z );
				const bvhvec3 o2( left->aabbMax.x - O.x, left->aabbMax.y - O.y, left->aabbMax.z - O.z );
				// 1. Early-in test: if first ray hits the node, the packet visits the node
				CALC_TMIN_TMAX_WITH_SLABTEST_ON_RAY( first );
				const bool earlyHit = (tmax >= tmin && tmin < packet[first].hit.t && tmax >= 0);
				distLeft = tmin;
				// 2. Early-out test: if the node aabb is outside the four planes, we skip the node
				if (!earlyHit)
				{
					float* minmax = (float*)left;
					bvhvec3 p0( minmax[sign0x], minmax[sign0y], minmax[sign0z] );
					bvhvec3 p1( minmax[sign1x], minmax[sign1y], minmax[sign1z] );
					bvhvec3 p2( minmax[sign2x], minmax[sign2y], minmax[sign2z] );
					bvhvec3 p3( minmax[sign3x], minmax[sign3y], minmax[sign3z] );
					if (dot( p0, plane0 ) > d0 || dot( p1, plane1 ) > d1 || dot( p2, plane2 ) > d2 || dot( p3, plane3 ) > d3)
						visitLeft = false;
					else
					{
						// 3. Last resort: update first and last, stay in node if first > last
						for (; leftFirst <= leftLast; leftFirst++)
						{
							CALC_TMIN_TMAX_WITH_SLABTEST_ON_RAY( leftFirst );
							if (tmax >= tmin && tmin < packet[leftFirst].hit.t && tmax >= 0) { distLeft = tmin; break; }
						}
						for (; leftLast >= leftFirst; leftLast--)
						{
							CALC_TMIN_TMAX_WITH_SLABTEST_ON_RAY( leftLast );
							if (tmax >= tmin && tmin < packet[leftLast].hit.t && tmax >= 0) break;
						}
						visitLeft = leftLast >= leftFirst;
					}
				}
			}
			{
				// see if we want to intersect the right child
				const bvhvec3 o1( right->aabbMin.x - O.x, right->aabbMin.y - O.y, right->aabbMin.z - O.z );
				const bvhvec3 o2( right->aabbMax.x - O.x, right->aabbMax.y - O.y, right->aabbMax.z - O.z );
				// 1. Early-in test: if first ray hits the node, the packet visits the node
				CALC_TMIN_TMAX_WITH_SLABTEST_ON_RAY( first );
				const bool earlyHit = (tmax >= tmin && tmin < packet[first].hit.t && tmax >= 0);
				distRight = tmin;
				// 2. Early-out test: if the node aabb is outside the four planes, we skip the node
				if (!earlyHit)
				{
					float* minmax = (float*)right;
					bvhvec3 p0( minmax[sign0x], minmax[sign0y], minmax[sign0z] );
					bvhvec3 p1( minmax[sign1x], minmax[sign1y], minmax[sign1z] );
					bvhvec3 p2( minmax[sign2x], minmax[sign2y], minmax[sign2z] );
					bvhvec3 p3( minmax[sign3x], minmax[sign3y], minmax[sign3z] );
					if (dot( p0, plane0 ) > d0 || dot( p1, plane1 ) > d1 || dot( p2, plane2 ) > d2 || dot( p3, plane3 ) > d3)
						visitRight = false;
					else
					{
						// 3. Last resort: update first and last, stay in node if first > last
						for (; rightFirst <= rightLast; rightFirst++)
						{
							CALC_TMIN_TMAX_WITH_SLABTEST_ON_RAY( rightFirst );
							if (tmax >= tmin && tmin < packet[rightFirst].hit.t && tmax >= 0) { distRight = tmin; break; }
						}
						for (; rightLast >= first; rightLast--)
						{
							CALC_TMIN_TMAX_WITH_SLABTEST_ON_RAY( rightLast );
							if (tmax >= tmin && tmin < packet[rightLast].hit.t && tmax >= 0) break;
						}
						visitRight = rightLast >= rightFirst;
					}
				}
			}
			// process intersection result
			if (visitLeft && visitRight)
			{
				if (distLeft < distRight)
				{
					// push right, continue with left
					stack[stackPtr++] = node->leftFirst + 1;
					stack[stackPtr++] = (rightFirst << 8) + rightLast;
					node = left, first = leftFirst, last = leftLast;
				}
				else
				{
					// push left, continue with right
					stack[stackPtr++] = node->leftFirst;
					stack[stackPtr++] = (leftFirst << 8) + leftLast;
					node = right, first = rightFirst, last = rightLast;
				}
			}
			else if (visitLeft) // continue with left
				node = left, first = leftFirst, last = leftLast;
			else if (visitRight) // continue with right
				node = right, first = rightFirst, last = rightLast;
			else if (stackPtr == 0) break; else // pop
				last = stack[--stackPtr], node = bvhNode + stack[--stackPtr],
				first = last >> 8, last &= 255;
		}
	}
}

// IntersectTri
void BVH::IntersectTri( Ray& ray, const unsigned idx ) const
{
	// Moeller-Trumbore ray/triangle intersection algorithm
	const unsigned vertIdx = idx * 3;
	const bvhvec3 edge1 = verts[vertIdx + 1] - verts[vertIdx];
	const bvhvec3 edge2 = verts[vertIdx + 2] - verts[vertIdx];
	const bvhvec3 h = cross( ray.D, edge2 );
	const float a = dot( edge1, h );
	if (fabs( a ) < 0.0000001f) return; // ray parallel to triangle
	const float f = 1 / a;
	const bvhvec3 s = ray.O - bvhvec3( verts[vertIdx] );
	const float u = f * dot( s, h );
	if (u < 0 || u > 1) return;
	const bvhvec3 q = cross( s, edge1 );
	const float v = f * dot( ray.D, q );
	if (v < 0 || u + v > 1) return;
	const float t = f * dot( edge2, q );
	if (t > 0 && t < ray.hit.t)
	{
		// register a hit: ray is shortened to t
		ray.hit.t = t, ray.hit.u = u, ray.hit.v = v, ray.hit.prim = idx;
	}
}

// IntersectAABB
float BVH::IntersectAABB( const Ray& ray, const bvhvec3& aabbMin, const bvhvec3& aabbMax )
{
	// "slab test" ray/AABB intersection
	float tx1 = (aabbMin.x - ray.O.x) * ray.rD.x, tx2 = (aabbMax.x - ray.O.x) * ray.rD.x;
	float tmin = tinybvh_min( tx1, tx2 ), tmax = tinybvh_max( tx1, tx2 );
	float ty1 = (aabbMin.y - ray.O.y) * ray.rD.y, ty2 = (aabbMax.y - ray.O.y) * ray.rD.y;
	tmin = tinybvh_max( tmin, tinybvh_min( ty1, ty2 ) );
	tmax = tinybvh_min( tmax, tinybvh_max( ty1, ty2 ) );
	float tz1 = (aabbMin.z - ray.O.z) * ray.rD.z, tz2 = (aabbMax.z - ray.O.z) * ray.rD.z;
	tmin = tinybvh_max( tmin, tinybvh_min( tz1, tz2 ) );
	tmax = tinybvh_min( tmax, tinybvh_max( tz1, tz2 ) );
	if (tmax >= tmin && tmin < ray.hit.t && tmax >= 0) return tmin; else return 1e30f;
}

// ============================================================================
//
//        I M P L E M E N T A T I O N  -  A V X / S S E  C O D E
// 
// ============================================================================

#ifdef BVH_USEAVX

// Ultra-fast single-threaded AVX binned-SAH-builder.
// This code produces BVHs nearly identical to reference, but much faster.
// On a 12th gen laptop i7 CPU, Sponza Crytek (~260k tris) is processed in 51ms.
// The code relies on the availability of AVX instructions. AVX2 is not needed.
#ifdef _MSC_VER
#define LANE(a,b) a.m128_f32[b]
#define LANE8(a,b) a.m256_f32[b]
// Not using clang/g++ method under MSCC; compiler may benefit from .m128_i32.
#define ILANE(a,b) a.m128i_i32[b]
#else
#define LANE(a,b) a[b]
#define LANE8(a,b) a[b]
// Below method reduces to a single instruction.
#define ILANE(a,b) _mm_cvtsi128_si32(_mm_castps_si128( _mm_shuffle_ps(_mm_castsi128_ps( a ), _mm_castsi128_ps( a ), b)))
#endif
inline float halfArea( const __m128 a /* a contains extent of aabb */ )
{
	return LANE( a, 0 ) * LANE( a, 1 ) + LANE( a, 1 ) * LANE( a, 2 ) + LANE( a, 2 ) * LANE( a, 3 );
}
inline float halfArea( const __m256& a /* a contains aabb itself, with min.xyz negated */ )
{
#ifndef _MSC_VER
	// g++ doesn't seem to like the faster construct
	float* c = (float*)&a;
	float ex = c[4] + c[0], ey = c[5] + c[1], ez = c[6] + c[2];
	return ex * ey + ey * ez + ez * ex;
#else
	const __m128 q = _mm256_castps256_ps128( _mm256_add_ps( _mm256_permute2f128_ps( a, a, 5 ), a ) );
	const __m128 v = _mm_mul_ps( q, _mm_shuffle_ps( q, q, 9 ) );
	return LANE( v, 0 ) + LANE( v, 1 ) + LANE( v, 2 );
#endif
}
#define PROCESS_PLANE( a, pos, ANLR, lN, rN, lb, rb ) if (lN * rN != 0) { \
	ANLR = halfArea( lb ) * (float)lN + halfArea( rb ) * (float)rN; \
	const float C = C_TRAV + C_INT * rSAV * ANLR; if (C < splitCost) \
	splitCost = C, bestAxis = a, bestPos = pos, bestLBox = lb, bestRBox = rb; }
#if defined(_MSC_VER)
#pragma warning ( push )
#pragma warning( disable:4701 ) // "potentially uninitialized local variable 'bestLBox' used"
#elif defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
void BVH::BuildAVX( const bvhvec4* vertices, const unsigned primCount )
{
	int test = BVHBINS;
	if (test != 8) assert( false ); // AVX builders require BVHBINS == 8.
	assert( ((long long)vertices & 63) == 0 ); // buffer must be cacheline-aligned
	// aligned data
	ALIGNED( 64 ) __m256 binbox[3 * BVHBINS];			// 768 bytes
	ALIGNED( 64 ) __m256 binboxOrig[3 * BVHBINS];		// 768 bytes
	ALIGNED( 64 ) unsigned count[3][BVHBINS]{};			// 96 bytes
	ALIGNED( 64 ) __m256 bestLBox, bestRBox;			// 64 bytes
	// some constants
	static const __m128 max4 = _mm_set1_ps( -1e30f ), half4 = _mm_set1_ps( 0.5f );
	static const __m128 two4 = _mm_set1_ps( 2.0f ), min1 = _mm_set1_ps( -1 );
	static const __m256 max8 = _mm256_set1_ps( -1e30f );
	static const __m256 signFlip8 = _mm256_setr_ps( -0.0f, -0.0f, -0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f );
	static const __m128 signFlip4 = _mm_setr_ps( -0.0f, -0.0f, -0.0f, 0.0f );
	static const __m128 mask3 = _mm_cmpeq_ps( _mm_setr_ps( 0, 0, 0, 1 ), _mm_setzero_ps() );
	static const __m128 binmul3 = _mm_set1_ps( BVHBINS * 0.49999f );
	for (unsigned i = 0; i < 3 * BVHBINS; i++) binboxOrig[i] = max8; // binbox initialization template
	// reset node pool
	const unsigned spaceNeeded = primCount * 2;
	if (allocatedBVHNodes < spaceNeeded)
	{
		AlignedFree( bvhNode );
		AlignedFree( triIdx );
		AlignedFree( fragment );
		verts = (bvhvec4*)vertices;
		triIdx = (unsigned*)AlignedAlloc( primCount * sizeof( unsigned ) );
		bvhNode = (BVHNode*)AlignedAlloc( spaceNeeded * sizeof( BVHNode ) );
		allocatedBVHNodes = spaceNeeded;
		memset( &bvhNode[1], 0, 32 ); // avoid crash in refit.
		fragment = (Fragment*)AlignedAlloc( primCount * sizeof( Fragment ) );
	}
	else FATAL_ERROR_IF( !rebuildable, "BVH::BuildAVX( .. ), bvh not rebuildable." );
	triCount = idxCount = primCount;
	unsigned newNodePtr = 2;
	struct FragSSE { __m128 bmin4, bmax4; };
	FragSSE* frag4 = (FragSSE*)fragment;
	__m256* frag8 = (__m256*)fragment;
	const __m128* verts4 = (__m128*)verts;
	// assign all triangles to the root node
	BVHNode& root = bvhNode[0];
	root.leftFirst = 0, root.triCount = triCount;
	// initialize fragments and update root bounds
	__m128 rootMin = max4, rootMax = max4;
	for (unsigned i = 0; i < triCount; i++)
	{
		const __m128 v1 = _mm_xor_ps( signFlip4, _mm_min_ps( _mm_min_ps( verts4[i * 3], verts4[i * 3 + 1] ), verts4[i * 3 + 2] ) );
		const __m128 v2 = _mm_max_ps( _mm_max_ps( verts4[i * 3], verts4[i * 3 + 1] ), verts4[i * 3 + 2] );
		frag4[i].bmin4 = v1, frag4[i].bmax4 = v2, rootMin = _mm_max_ps( rootMin, v1 ), rootMax = _mm_max_ps( rootMax, v2 ), triIdx[i] = i;
	}
	rootMin = _mm_xor_ps( rootMin, signFlip4 );
	root.aabbMin = *(bvhvec3*)&rootMin, root.aabbMax = *(bvhvec3*)&rootMax;
	// subdivide recursively
	ALIGNED( 64 ) unsigned task[128], taskCount = 0, nodeIdx = 0;
	const bvhvec3 minDim = (root.aabbMax - root.aabbMin) * 1e-7f;
	while (1)
	{
		while (1)
		{
			BVHNode& node = bvhNode[nodeIdx];
			__m128* node4 = (__m128*) & bvhNode[nodeIdx];
			// find optimal object split
			const __m128 d4 = _mm_blendv_ps( min1, _mm_sub_ps( node4[1], node4[0] ), mask3 );
			const __m128 nmin4 = _mm_mul_ps( _mm_and_ps( node4[0], mask3 ), two4 );
			const __m128 rpd4 = _mm_and_ps( _mm_div_ps( binmul3, d4 ), _mm_cmpneq_ps( d4, _mm_setzero_ps() ) );
			// implementation of Section 4.1 of "Parallel Spatial Splits in Bounding Volume Hierarchies":
			// main loop operates on two fragments to minimize dependencies and maximize ILP.
			unsigned fi = triIdx[node.leftFirst];
			memset( count, 0, sizeof( count ) );
			__m256 r0, r1, r2, f = frag8[fi];
			__m128i bi4 = _mm_cvtps_epi32( _mm_sub_ps( _mm_mul_ps( _mm_sub_ps( _mm_sub_ps( frag4[fi].bmax4, frag4[fi].bmin4 ), nmin4 ), rpd4 ), half4 ) );
			memcpy( binbox, binboxOrig, sizeof( binbox ) );
			unsigned i0 = ILANE( bi4, 0 ), i1 = ILANE( bi4, 1 ), i2 = ILANE( bi4, 2 ), * ti = triIdx + node.leftFirst + 1;
			for (unsigned i = 0; i < node.triCount - 1; i++)
			{
				unsigned fid = *ti++;
			#if defined __GNUC__ || _MSC_VER < 1920
				if (fid > triCount) fid = triCount - 1; // never happens but g++ *and* vs2017 need this to not crash...
			#endif
				const __m256 b0 = binbox[i0], b1 = binbox[BVHBINS + i1], b2 = binbox[2 * BVHBINS + i2];
				const __m128 fmin = frag4[fid].bmin4, fmax = frag4[fid].bmax4;
				r0 = _mm256_max_ps( b0, f ), r1 = _mm256_max_ps( b1, f ), r2 = _mm256_max_ps( b2, f );
				const __m128i b4 = _mm_cvtps_epi32( _mm_sub_ps( _mm_mul_ps( _mm_sub_ps( _mm_sub_ps( fmax, fmin ), nmin4 ), rpd4 ), half4 ) );
				f = frag8[fid], count[0][i0]++, count[1][i1]++, count[2][i2]++;
				binbox[i0] = r0, i0 = ILANE( b4, 0 );
				binbox[BVHBINS + i1] = r1, i1 = ILANE( b4, 1 );
				binbox[2 * BVHBINS + i2] = r2, i2 = ILANE( b4, 2 );
			}
			// final business for final fragment
			const __m256 b0 = binbox[i0], b1 = binbox[BVHBINS + i1], b2 = binbox[2 * BVHBINS + i2];
			count[0][i0]++, count[1][i1]++, count[2][i2]++;
			r0 = _mm256_max_ps( b0, f ), r1 = _mm256_max_ps( b1, f ), r2 = _mm256_max_ps( b2, f );
			binbox[i0] = r0, binbox[BVHBINS + i1] = r1, binbox[2 * BVHBINS + i2] = r2;
			// calculate per-split totals
			float splitCost = 1e30f, rSAV = 1.0f / node.SurfaceArea();
			unsigned bestAxis = 0, bestPos = 0, n = newNodePtr, j = node.leftFirst + node.triCount, src = node.leftFirst;
			const __m256* bb = binbox;
			for (int a = 0; a < 3; a++, bb += BVHBINS) if ((node.aabbMax[a] - node.aabbMin[a]) > minDim.cell[a])
			{
				// hardcoded bin processing for BVHBINS == 8
				assert( BVHBINS == 8 );
				const unsigned lN0 = count[a][0], rN0 = count[a][7];
				const __m256 lb0 = bb[0], rb0 = bb[7];
				const unsigned lN1 = lN0 + count[a][1], rN1 = rN0 + count[a][6], lN2 = lN1 + count[a][2];
				const unsigned rN2 = rN1 + count[a][5], lN3 = lN2 + count[a][3], rN3 = rN2 + count[a][4];
				const __m256 lb1 = _mm256_max_ps( lb0, bb[1] ), rb1 = _mm256_max_ps( rb0, bb[6] );
				const __m256 lb2 = _mm256_max_ps( lb1, bb[2] ), rb2 = _mm256_max_ps( rb1, bb[5] );
				const __m256 lb3 = _mm256_max_ps( lb2, bb[3] ), rb3 = _mm256_max_ps( rb2, bb[4] );
				const unsigned lN4 = lN3 + count[a][4], rN4 = rN3 + count[a][3], lN5 = lN4 + count[a][5];
				const unsigned rN5 = rN4 + count[a][2], lN6 = lN5 + count[a][6], rN6 = rN5 + count[a][1];
				const __m256 lb4 = _mm256_max_ps( lb3, bb[4] ), rb4 = _mm256_max_ps( rb3, bb[3] );
				const __m256 lb5 = _mm256_max_ps( lb4, bb[5] ), rb5 = _mm256_max_ps( rb4, bb[2] );
				const __m256 lb6 = _mm256_max_ps( lb5, bb[6] ), rb6 = _mm256_max_ps( rb5, bb[1] );
				float ANLR3 = 1e30f; PROCESS_PLANE( a, 3, ANLR3, lN3, rN3, lb3, rb3 ); // most likely split
				float ANLR2 = 1e30f; PROCESS_PLANE( a, 2, ANLR2, lN2, rN4, lb2, rb4 );
				float ANLR4 = 1e30f; PROCESS_PLANE( a, 4, ANLR4, lN4, rN2, lb4, rb2 );
				float ANLR5 = 1e30f; PROCESS_PLANE( a, 5, ANLR5, lN5, rN1, lb5, rb1 );
				float ANLR1 = 1e30f; PROCESS_PLANE( a, 1, ANLR1, lN1, rN5, lb1, rb5 );
				float ANLR0 = 1e30f; PROCESS_PLANE( a, 0, ANLR0, lN0, rN6, lb0, rb6 );
				float ANLR6 = 1e30f; PROCESS_PLANE( a, 6, ANLR6, lN6, rN0, lb6, rb0 ); // least likely split
			}
			float noSplitCost = (float)node.triCount * C_INT;
			if (splitCost >= noSplitCost) break; // not splitting is better.
			// in-place partition
			const float rpd = (*(bvhvec3*)&rpd4)[bestAxis], nmin = (*(bvhvec3*)&nmin4)[bestAxis];
			unsigned t, fr = triIdx[src];
			for (unsigned i = 0; i < node.triCount; i++)
			{
				const unsigned bi = (unsigned)((fragment[fr].bmax[bestAxis] - fragment[fr].bmin[bestAxis] - nmin) * rpd);
				if (bi <= bestPos) fr = triIdx[++src]; else t = fr, fr = triIdx[src] = triIdx[--j], triIdx[j] = t;
			}
			// create child nodes and recurse
			const unsigned leftCount = src - node.leftFirst, rightCount = node.triCount - leftCount;
			if (leftCount == 0 || rightCount == 0) break; // should not happen.
			*(__m256*)& bvhNode[n] = _mm256_xor_ps( bestLBox, signFlip8 );
			bvhNode[n].leftFirst = node.leftFirst, bvhNode[n].triCount = leftCount;
			node.leftFirst = n++, node.triCount = 0, newNodePtr += 2;
			*(__m256*)& bvhNode[n] = _mm256_xor_ps( bestRBox, signFlip8 );
			bvhNode[n].leftFirst = j, bvhNode[n].triCount = rightCount;
			task[taskCount++] = n, nodeIdx = n - 1;
		}
		// fetch subdivision task from stack
		if (taskCount == 0) break; else nodeIdx = task[--taskCount];
	}
	// all done.
	refittable = true; // not using spatial splits: can refit this BVH
	frag_min_flipped = true; // AVX was used for binning; fragment.min flipped
	may_have_holes = false; // the AVX builder produces a continuous list of nodes
	usedBVHNodes = newNodePtr;
}
#if defined(_MSC_VER)
#pragma warning ( pop ) // restore 4701
#elif defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop // restore -Wmaybe-uninitialized
#endif

// Intersect a BVH with a ray packet, basic SSE-optimized version.
// Note: This yields +10% on 10th gen Intel CPUs, but a small loss on
// more recent hardware. This function needs a full conversion to work
// with groups of 8 rays at a time - TODO.
void BVH::Intersect256RaysSSE( Ray* packet ) const
{
	// Corner rays are: 0, 51, 204 and 255
	// Construct the bounding planes, with normals pointing outwards
	bvhvec3 O = packet[0].O; // same for all rays in this case
	__m128 O4 = *(__m128*) & packet[0].O;
	__m128 mask4 = _mm_cmpeq_ps( _mm_setzero_ps(), _mm_set_ps( 1, 0, 0, 0 ) );
	bvhvec3 p0 = packet[0].O + packet[0].D; // top-left
	bvhvec3 p1 = packet[51].O + packet[51].D; // top-right
	bvhvec3 p2 = packet[204].O + packet[204].D; // bottom-left
	bvhvec3 p3 = packet[255].O + packet[255].D; // bottom-right
	bvhvec3 plane0 = normalize( cross( p0 - O, p0 - p2 ) ); // left plane
	bvhvec3 plane1 = normalize( cross( p3 - O, p3 - p1 ) ); // right plane
	bvhvec3 plane2 = normalize( cross( p1 - O, p1 - p0 ) ); // top plane
	bvhvec3 plane3 = normalize( cross( p2 - O, p2 - p3 ) ); // bottom plane
	int sign0x = plane0.x < 0 ? 4 : 0, sign0y = plane0.y < 0 ? 5 : 1, sign0z = plane0.z < 0 ? 6 : 2;
	int sign1x = plane1.x < 0 ? 4 : 0, sign1y = plane1.y < 0 ? 5 : 1, sign1z = plane1.z < 0 ? 6 : 2;
	int sign2x = plane2.x < 0 ? 4 : 0, sign2y = plane2.y < 0 ? 5 : 1, sign2z = plane2.z < 0 ? 6 : 2;
	int sign3x = plane3.x < 0 ? 4 : 0, sign3y = plane3.y < 0 ? 5 : 1, sign3z = plane3.z < 0 ? 6 : 2;
	float t0 = dot( O, plane0 ), t1 = dot( O, plane1 );
	float t2 = dot( O, plane2 ), t3 = dot( O, plane3 );
	// Traverse the tree with the packet
	int first = 0, last = 255; // first and last active ray in the packet
	BVHNode* node = &bvhNode[0];
	ALIGNED( 64 ) unsigned stack[64], stackPtr = 0;
	while (1)
	{
		if (node->isLeaf())
		{
			// handle leaf node
			for (unsigned j = 0; j < node->triCount; j++)
			{
				const unsigned idx = triIdx[node->leftFirst + j], vid = idx * 3;
				const bvhvec3 edge1 = verts[vid + 1] - verts[vid], edge2 = verts[vid + 2] - verts[vid];
				const bvhvec3 s = O - bvhvec3( verts[vid] );
				for (int i = first; i <= last; i++)
				{
					Ray& ray = packet[i];
					const bvhvec3 h = cross( ray.D, edge2 );
					const float a = dot( edge1, h );
					if (fabs( a ) < 0.0000001f) continue; // ray parallel to triangle
					const float f = 1 / a, u = f * dot( s, h );
					if (u < 0 || u > 1) continue;
					const bvhvec3 q = cross( s, edge1 );
					const float v = f * dot( ray.D, q );
					if (v < 0 || u + v > 1) continue;
					const float t = f * dot( edge2, q );
					if (t <= 0 || t >= ray.hit.t) continue;
					ray.hit.t = t, ray.hit.u = u, ray.hit.v = v, ray.hit.prim = idx;
				}
			}
			if (stackPtr == 0) break; else // pop
				last = stack[--stackPtr], node = bvhNode + stack[--stackPtr],
				first = last >> 8, last &= 255;
		}
		else
		{
			// fetch pointers to child nodes
			BVHNode* left = bvhNode + node->leftFirst;
			BVHNode* right = bvhNode + node->leftFirst + 1;
			bool visitLeft = true, visitRight = true;
			int leftFirst = first, leftLast = last, rightFirst = first, rightLast = last;
			float distLeft, distRight;
			{
				// see if we want to intersect the left child
				const __m128 minO4 = _mm_sub_ps( *(__m128*) & left->aabbMin, O4 );
				const __m128 maxO4 = _mm_sub_ps( *(__m128*) & left->aabbMax, O4 );
				// 1. Early-in test: if first ray hits the node, the packet visits the node
				const __m128 rD4 = *(__m128*) & packet[first].rD;
				const __m128 st1 = _mm_mul_ps( _mm_and_ps( minO4, mask4 ), rD4 );
				const __m128 st2 = _mm_mul_ps( _mm_and_ps( maxO4, mask4 ), rD4 );
				const __m128 vmax4 = _mm_max_ps( st1, st2 ), vmin4 = _mm_min_ps( st1, st2 );
				const float tmax = tinybvh_min( LANE( vmax4, 0 ), tinybvh_min( LANE( vmax4, 1 ), LANE( vmax4, 2 ) ) );
				const float tmin = tinybvh_max( LANE( vmin4, 0 ), tinybvh_max( LANE( vmin4, 1 ), LANE( vmin4, 2 ) ) );
				const bool earlyHit = (tmax >= tmin && tmin < packet[first].hit.t && tmax >= 0);
				distLeft = tmin;
				// 2. Early-out test: if the node aabb is outside the four planes, we skip the node
				if (!earlyHit)
				{
					float* minmax = (float*)left;
					bvhvec3 p0( minmax[sign0x], minmax[sign0y], minmax[sign0z] );
					bvhvec3 p1( minmax[sign1x], minmax[sign1y], minmax[sign1z] );
					bvhvec3 p2( minmax[sign2x], minmax[sign2y], minmax[sign2z] );
					bvhvec3 p3( minmax[sign3x], minmax[sign3y], minmax[sign3z] );
					if (dot( p0, plane0 ) > t0 || dot( p1, plane1 ) > t1 || dot( p2, plane2 ) > t2 || dot( p3, plane3 ) > t3)
						visitLeft = false;
					else
					{
						// 3. Last resort: update first and last, stay in node if first > last
						for (; leftFirst <= leftLast; leftFirst++)
						{
							const __m128 rD4 = *(__m128*) & packet[leftFirst].rD;
							const __m128 st1 = _mm_mul_ps( _mm_and_ps( minO4, mask4 ), rD4 );
							const __m128 st2 = _mm_mul_ps( _mm_and_ps( maxO4, mask4 ), rD4 );
							const __m128 vmax4 = _mm_max_ps( st1, st2 ), vmin4 = _mm_min_ps( st1, st2 );
							const float tmax = tinybvh_min( LANE( vmax4, 0 ), tinybvh_min( LANE( vmax4, 1 ), LANE( vmax4, 2 ) ) );
							const float tmin = tinybvh_max( LANE( vmin4, 0 ), tinybvh_max( LANE( vmin4, 1 ), LANE( vmin4, 2 ) ) );
							if (tmax >= tmin && tmin < packet[leftFirst].hit.t && tmax >= 0) { distLeft = tmin; break; }
						}
						for (; leftLast >= leftFirst; leftLast--)
						{
							const __m128 rD4 = *(__m128*) & packet[leftLast].rD;
							const __m128 st1 = _mm_mul_ps( _mm_and_ps( minO4, mask4 ), rD4 );
							const __m128 st2 = _mm_mul_ps( _mm_and_ps( maxO4, mask4 ), rD4 );
							const __m128 vmax4 = _mm_max_ps( st1, st2 ), vmin4 = _mm_min_ps( st1, st2 );
							const float tmax = tinybvh_min( LANE( vmax4, 0 ), tinybvh_min( LANE( vmax4, 1 ), LANE( vmax4, 2 ) ) );
							const float tmin = tinybvh_max( LANE( vmin4, 0 ), tinybvh_max( LANE( vmin4, 1 ), LANE( vmin4, 2 ) ) );
							if (tmax >= tmin && tmin < packet[leftLast].hit.t && tmax >= 0) break;
						}
						visitLeft = leftLast >= leftFirst;
					}
				}
			}
			{
				// see if we want to intersect the right child
				const __m128 minO4 = _mm_sub_ps( *(__m128*) & right->aabbMin, O4 );
				const __m128 maxO4 = _mm_sub_ps( *(__m128*) & right->aabbMax, O4 );
				// 1. Early-in test: if first ray hits the node, the packet visits the node
				const __m128 rD4 = *(__m128*) & packet[first].rD;
				const __m128 st1 = _mm_mul_ps( minO4, rD4 ), st2 = _mm_mul_ps( maxO4, rD4 );
				const __m128 vmax4 = _mm_max_ps( st1, st2 ), vmin4 = _mm_min_ps( st1, st2 );
				const float tmax = tinybvh_min( LANE( vmax4, 0 ), tinybvh_min( LANE( vmax4, 1 ), LANE( vmax4, 2 ) ) );
				const float tmin = tinybvh_max( LANE( vmin4, 0 ), tinybvh_max( LANE( vmin4, 1 ), LANE( vmin4, 2 ) ) );
				const bool earlyHit = (tmax >= tmin && tmin < packet[first].hit.t && tmax >= 0);
				distRight = tmin;
				// 2. Early-out test: if the node aabb is outside the four planes, we skip the node
				if (!earlyHit)
				{
					float* minmax = (float*)right;
					bvhvec3 p0( minmax[sign0x], minmax[sign0y], minmax[sign0z] );
					bvhvec3 p1( minmax[sign1x], minmax[sign1y], minmax[sign1z] );
					bvhvec3 p2( minmax[sign2x], minmax[sign2y], minmax[sign2z] );
					bvhvec3 p3( minmax[sign3x], minmax[sign3y], minmax[sign3z] );
					if (dot( p0, plane0 ) > t0 || dot( p1, plane1 ) > t1 || dot( p2, plane2 ) > t2 || dot( p3, plane3 ) > t3)
						visitRight = false;
					else
					{
						// 3. Last resort: update first and last, stay in node if first > last
						for (; rightFirst <= rightLast; rightFirst++)
						{
							const __m128 rD4 = *(__m128*) & packet[rightFirst].rD;
							const __m128 st1 = _mm_mul_ps( _mm_and_ps( minO4, mask4 ), rD4 );
							const __m128 st2 = _mm_mul_ps( _mm_and_ps( maxO4, mask4 ), rD4 );
							const __m128 vmax4 = _mm_max_ps( st1, st2 ), vmin4 = _mm_min_ps( st1, st2 );
							const float tmax = tinybvh_min( LANE( vmax4, 0 ), tinybvh_min( LANE( vmax4, 1 ), LANE( vmax4, 2 ) ) );
							const float tmin = tinybvh_max( LANE( vmin4, 0 ), tinybvh_max( LANE( vmin4, 1 ), LANE( vmin4, 2 ) ) );
							if (tmax >= tmin && tmin < packet[rightFirst].hit.t && tmax >= 0) { distRight = tmin; break; }
						}
						for (; rightLast >= first; rightLast--)
						{
							const __m128 rD4 = *(__m128*) & packet[rightLast].rD;
							const __m128 st1 = _mm_mul_ps( _mm_and_ps( minO4, mask4 ), rD4 );
							const __m128 st2 = _mm_mul_ps( _mm_and_ps( maxO4, mask4 ), rD4 );
							const __m128 vmax4 = _mm_max_ps( st1, st2 ), vmin4 = _mm_min_ps( st1, st2 );
							const float tmax = tinybvh_min( LANE( vmax4, 0 ), tinybvh_min( LANE( vmax4, 1 ), LANE( vmax4, 2 ) ) );
							const float tmin = tinybvh_max( LANE( vmin4, 0 ), tinybvh_max( LANE( vmin4, 1 ), LANE( vmin4, 2 ) ) );
							if (tmax >= tmin && tmin < packet[rightLast].hit.t && tmax >= 0) break;
						}
						visitRight = rightLast >= rightFirst;
					}
				}
			}
			// process intersection result
			if (visitLeft && visitRight)
			{
				if (distLeft < distRight)
				{
					// push right, continue with left
					stack[stackPtr++] = node->leftFirst + 1;
					stack[stackPtr++] = (rightFirst << 8) + rightLast;
					node = left, first = leftFirst, last = leftLast;
				}
				else
				{
					// push left, continue with right
					stack[stackPtr++] = node->leftFirst;
					stack[stackPtr++] = (leftFirst << 8) + leftLast;
					node = right, first = rightFirst, last = rightLast;
				}
			}
			else if (visitLeft) // continue with left
				node = left, first = leftFirst, last = leftLast;
			else if (visitRight) // continue with right
				node = right, first = rightFirst, last = rightLast;
			else if (stackPtr == 0) break; else // pop
				last = stack[--stackPtr], node = bvhNode + stack[--stackPtr],
				first = last >> 8, last &= 255;
		}
	}
}

// Traverse the second alternative BVH layout (ALT_SOA).
int BVH::Intersect_AltSoA( Ray& ray ) const
{
	BVHNodeAlt2* node = &alt2Node[0], * stack[64];
	unsigned stackPtr = 0, steps = 0;
	const __m128 Ox4 = _mm_set1_ps( ray.O.x ), rDx4 = _mm_set1_ps( ray.rD.x );
	const __m128 Oy4 = _mm_set1_ps( ray.O.y ), rDy4 = _mm_set1_ps( ray.rD.y );
	const __m128 Oz4 = _mm_set1_ps( ray.O.z ), rDz4 = _mm_set1_ps( ray.rD.z );
	// const __m128 inf4 = _mm_set1_ps( 1e30f );
	while (1)
	{
		steps++;
		if (node->isLeaf())
		{
			for (unsigned i = 0; i < node->triCount; i++)
			{
				const unsigned tidx = triIdx[node->firstTri + i], vertIdx = tidx * 3;
				const bvhvec3 edge1 = verts[vertIdx + 1] - verts[vertIdx];
				const bvhvec3 edge2 = verts[vertIdx + 2] - verts[vertIdx];
				const bvhvec3 h = cross( ray.D, edge2 );
				const float a = dot( edge1, h );
				if (fabs( a ) < 0.0000001f) continue; // ray parallel to triangle
				const float f = 1 / a;
				const bvhvec3 s = ray.O - bvhvec3( verts[vertIdx] );
				const float u = f * dot( s, h );
				if (u < 0 || u > 1) continue;
				const bvhvec3 q = cross( s, edge1 );
				const float v = f * dot( ray.D, q );
				if (v < 0 || u + v > 1) continue;
				const float t = f * dot( edge2, q );
				if (t < 0 || t > ray.hit.t) continue;
				ray.hit.t = t, ray.hit.u = u, ray.hit.v = v, ray.hit.prim = tidx;
			}
			if (stackPtr == 0) break; else node = stack[--stackPtr];
			continue;
		}
		__m128 x4 = _mm_mul_ps( _mm_sub_ps( node->xxxx, Ox4 ), rDx4 );
		__m128 y4 = _mm_mul_ps( _mm_sub_ps( node->yyyy, Oy4 ), rDy4 );
		__m128 z4 = _mm_mul_ps( _mm_sub_ps( node->zzzz, Oz4 ), rDz4 );
		// transpose
		__m128 t0 = _mm_unpacklo_ps( x4, y4 ), t2 = _mm_unpacklo_ps( z4, z4 );
		__m128 t1 = _mm_unpackhi_ps( x4, y4 ), t3 = _mm_unpackhi_ps( z4, z4 );
		__m128 xyzw1a = _mm_shuffle_ps( t0, t2, _MM_SHUFFLE( 1, 0, 1, 0 ) );
		__m128 xyzw2a = _mm_shuffle_ps( t0, t2, _MM_SHUFFLE( 3, 2, 3, 2 ) );
		__m128 xyzw1b = _mm_shuffle_ps( t1, t3, _MM_SHUFFLE( 1, 0, 1, 0 ) );
		__m128 xyzw2b = _mm_shuffle_ps( t1, t3, _MM_SHUFFLE( 3, 2, 3, 2 ) );
		// process
		__m128 tmina4 = _mm_min_ps( xyzw1a, xyzw2a ), tmaxa4 = _mm_max_ps( xyzw1a, xyzw2a );
		__m128 tminb4 = _mm_min_ps( xyzw1b, xyzw2b ), tmaxb4 = _mm_max_ps( xyzw1b, xyzw2b );
		// transpose back
		t0 = _mm_unpacklo_ps( tmina4, tmaxa4 ), t2 = _mm_unpacklo_ps( tminb4, tmaxb4 );
		t1 = _mm_unpackhi_ps( tmina4, tmaxa4 ), t3 = _mm_unpackhi_ps( tminb4, tmaxb4 );
		x4 = _mm_shuffle_ps( t0, t2, _MM_SHUFFLE( 1, 0, 1, 0 ) );
		y4 = _mm_shuffle_ps( t0, t2, _MM_SHUFFLE( 3, 2, 3, 2 ) );
		z4 = _mm_shuffle_ps( t1, t3, _MM_SHUFFLE( 1, 0, 1, 0 ) );
		unsigned lidx = node->left, ridx = node->right;
		const __m128 min4 = _mm_max_ps( _mm_max_ps( _mm_max_ps( x4, y4 ), z4 ), _mm_setzero_ps() );
		const __m128 max4 = _mm_min_ps( _mm_min_ps( _mm_min_ps( x4, y4 ), z4 ), _mm_set1_ps( ray.hit.t ) );
	#if 0
		// TODO: why is this slower on gen14?
		const float tmina_0 = LANE( min4, 0 ), tmaxa_1 = LANE( max4, 1 );
		const float tminb_2 = LANE( min4, 2 ), tmaxb_3 = LANE( max4, 3 );
		t0 = _mm_shuffle_ps( max4, max4, _MM_SHUFFLE( 1, 3, 1, 3 ) );
		t1 = _mm_shuffle_ps( min4, min4, _MM_SHUFFLE( 0, 2, 0, 2 ) );
		t0 = _mm_blendv_ps( inf4, t1, _mm_cmpge_ps( t0, t1 ) );
		float dist1 = LANE( t0, 1 ), dist2 = LANE( t0, 0 );
	#else
		const float tmina_0 = LANE( min4, 0 ), tmaxa_1 = LANE( max4, 1 );
		const float tminb_2 = LANE( min4, 2 ), tmaxb_3 = LANE( max4, 3 );
		float dist1 = tmaxa_1 >= tmina_0 ? tmina_0 : 1e30f;
		float dist2 = tmaxb_3 >= tminb_2 ? tminb_2 : 1e30f;
	#endif
		if (dist1 > dist2)
		{
			float t = dist1; dist1 = dist2; dist2 = t;
			unsigned i = lidx; lidx = ridx; ridx = i;
		}
		if (dist1 == 1e30f)
		{
			if (stackPtr == 0) break; else node = stack[--stackPtr];
		}
		else
		{
			node = alt2Node + lidx;
			if (dist2 != 1e30f) stack[stackPtr++] = alt2Node + ridx;
		}
	}
	return steps;
}

// Intersect_CWBVH:
// Intersect a compressed 8-wide BVH with a ray. For debugging only, not efficient.
// Not technically limited to BVH_USEAVX, but __lzcnt and __popcnt will require
// exotic compiler flags (in combination with __builtin_ia32_lzcnt_u32), so... Since
// this is just here to test data before it goes to the GPU: MSVC-only for now.
static unsigned __bfind( unsigned x ) // https://github.com/mackron/refcode/blob/master/lzcnt.c
{
#if defined(_MSC_VER) && !defined(__clang__)
	return 31 - __lzcnt( x );
#elif defined(__EMSCRIPTEN__)
	return 31 - __builtin_clz( x );
#elif defined(__GNUC__) || defined(__clang__)
	unsigned r;
	__asm__ __volatile__( "lzcnt{l %1, %0| %0, %1}" : "=r"(r) : "r"(x) : "cc" );
	return 31 - r;
#endif
}
static unsigned __popc( unsigned x )
{
#if defined(_MSC_VER) && !defined(__clang__)
	return __popcnt( x );
#elif defined(__GNUC__) || defined(__clang__)
	return __builtin_popcount( x );
#endif
}
#define STACK_POP() { ngroup = traversalStack[--stackPtr]; }
#define STACK_PUSH() { traversalStack[stackPtr++] = ngroup; }
static inline unsigned extract_byte( const unsigned i, const unsigned n ) { return (i >> (n * 8)) & 0xFF; }
static inline unsigned sign_extend_s8x4( const unsigned i )
{
	// asm("prmt.b32 %0, %1, 0x0, 0x0000BA98;" : "=r"(v) : "r"(i)); // BA98: 1011`1010`1001`1000 
	// with the given parameters, prmt will extend the sign to all bits in a byte. 
	unsigned b0 = (i & 0b10000000000000000000000000000000) ? 0xff000000 : 0;
	unsigned b1 = (i & 0b00000000100000000000000000000000) ? 0x00ff0000 : 0;
	unsigned b2 = (i & 0b00000000000000001000000000000000) ? 0x0000ff00 : 0;
	unsigned b3 = (i & 0b00000000000000000000000010000000) ? 0x000000ff : 0;
	return b0 + b1 + b2 + b3; // probably can do better than this.
}
int BVH::Intersect_CWBVH( Ray& ray ) const
{
	bvhuint2 traversalStack[128];
	unsigned hitAddr = 0, stackPtr = 0;
	bvhvec2 triangleuv( 0, 0 );
	const bvhvec4* blasNodes = bvh8Compact;
	const bvhvec4* blasTris = bvh8Tris;
	float tmin = 0, tmax = ray.hit.t;
	const unsigned octinv = (7 - ((ray.D.x < 0 ? 4 : 0) | (ray.D.y < 0 ? 2 : 0) | (ray.D.z < 0 ? 1 : 0))) * 0x1010101;
	bvhuint2 ngroup = bvhuint2( 0, 0b10000000000000000000000000000000 ), tgroup = bvhuint2( 0 );
	do
	{
		if (ngroup.y > 0x00FFFFFF)
		{
			const unsigned hits = ngroup.y, imask = ngroup.y;
			const unsigned child_bit_index = __bfind( hits );
			const unsigned child_node_base_index = ngroup.x;
			ngroup.y &= ~(1 << child_bit_index);
			if (ngroup.y > 0x00FFFFFF) { STACK_PUSH( /* nodeGroup */ ); }
			{
				const unsigned slot_index = (child_bit_index - 24) ^ (octinv & 255);
				const unsigned relative_index = __popc( imask & ~(0xFFFFFFFF << slot_index) );
				const unsigned child_node_index = child_node_base_index + relative_index;
				const bvhvec4 n0 = blasNodes[child_node_index * 5 + 0], n1 = blasNodes[child_node_index * 5 + 1];
				const bvhvec4 n2 = blasNodes[child_node_index * 5 + 2], n3 = blasNodes[child_node_index * 5 + 3];
				const bvhvec4 n4 = blasNodes[child_node_index * 5 + 4], p = n0;
			#ifdef __GNUC__
			#pragma GCC diagnostic push
			#pragma GCC diagnostic ignored "-Wstrict-aliasing"
			#endif
				bvhint3 e;
				e.x = (int)*((char*)&n0.w + 0), e.y = (int)*((char*)&n0.w + 1), e.z = (int)*((char*)&n0.w + 2);
				ngroup.x = as_uint( n1.x ), tgroup.x = as_uint( n1.y ), tgroup.y = 0;
				unsigned hitmask = 0;
				const unsigned vx = (e.x + 127) << 23u; const float adjusted_idirx = *(float*)&vx * ray.rD.x;
				const unsigned vy = (e.y + 127) << 23u; const float adjusted_idiry = *(float*)&vy * ray.rD.y;
				const unsigned vz = (e.z + 127) << 23u; const float adjusted_idirz = *(float*)&vz * ray.rD.z;
				const float origx = -(ray.O.x - p.x) * ray.rD.x;
				const float origy = -(ray.O.y - p.y) * ray.rD.y;
				const float origz = -(ray.O.z - p.z) * ray.rD.z;
				{	// First 4
					const unsigned meta4 = *(unsigned*)&n1.z, is_inner4 = (meta4 & (meta4 << 1)) & 0x10101010;
					const unsigned inner_mask4 = sign_extend_s8x4( is_inner4 << 3 );
					const unsigned bit_index4 = (meta4 ^ (octinv & inner_mask4)) & 0x1F1F1F1F;
					const unsigned child_bits4 = (meta4 >> 5) & 0x07070707;
					unsigned swizzledLox = (ray.rD.x < 0) ? *(unsigned*)&n3.z : *(unsigned*)&n2.x, swizzledHix = (ray.rD.x < 0) ? *(unsigned*)&n2.x : *(unsigned*)&n3.z;
					unsigned swizzledLoy = (ray.rD.y < 0) ? *(unsigned*)&n4.x : *(unsigned*)&n2.z, swizzledHiy = (ray.rD.y < 0) ? *(unsigned*)&n2.z : *(unsigned*)&n4.x;
					unsigned swizzledLoz = (ray.rD.z < 0) ? *(unsigned*)&n4.z : *(unsigned*)&n3.x, swizzledHiz = (ray.rD.z < 0) ? *(unsigned*)&n3.x : *(unsigned*)&n4.z;
					float tminx[4], tminy[4], tminz[4], tmaxx[4], tmaxy[4], tmaxz[4];
					tminx[0] = ((swizzledLox >> 0) & 0xFF) * adjusted_idirx + origx, tminx[1] = ((swizzledLox >> 8) & 0xFF) * adjusted_idirx + origx, tminx[2] = ((swizzledLox >> 16) & 0xFF) * adjusted_idirx + origx;
					tminx[3] = ((swizzledLox >> 24) & 0xFF) * adjusted_idirx + origx, tminy[0] = ((swizzledLoy >> 0) & 0xFF) * adjusted_idiry + origy, tminy[1] = ((swizzledLoy >> 8) & 0xFF) * adjusted_idiry + origy;
					tminy[2] = ((swizzledLoy >> 16) & 0xFF) * adjusted_idiry + origy, tminy[3] = ((swizzledLoy >> 24) & 0xFF) * adjusted_idiry + origy, tminz[0] = ((swizzledLoz >> 0) & 0xFF) * adjusted_idirz + origz;
					tminz[1] = ((swizzledLoz >> 8) & 0xFF) * adjusted_idirz + origz, tminz[2] = ((swizzledLoz >> 16) & 0xFF) * adjusted_idirz + origz, tminz[3] = ((swizzledLoz >> 24) & 0xFF) * adjusted_idirz + origz;
					tmaxx[0] = ((swizzledHix >> 0) & 0xFF) * adjusted_idirx + origx, tmaxx[1] = ((swizzledHix >> 8) & 0xFF) * adjusted_idirx + origx, tmaxx[2] = ((swizzledHix >> 16) & 0xFF) * adjusted_idirx + origx;
					tmaxx[3] = ((swizzledHix >> 24) & 0xFF) * adjusted_idirx + origx, tmaxy[0] = ((swizzledHiy >> 0) & 0xFF) * adjusted_idiry + origy, tmaxy[1] = ((swizzledHiy >> 8) & 0xFF) * adjusted_idiry + origy;
					tmaxy[2] = ((swizzledHiy >> 16) & 0xFF) * adjusted_idiry + origy, tmaxy[3] = ((swizzledHiy >> 24) & 0xFF) * adjusted_idiry + origy, tmaxz[0] = ((swizzledHiz >> 0) & 0xFF) * adjusted_idirz + origz;
					tmaxz[1] = ((swizzledHiz >> 8) & 0xFF) * adjusted_idirz + origz, tmaxz[2] = ((swizzledHiz >> 16) & 0xFF) * adjusted_idirz + origz, tmaxz[3] = ((swizzledHiz >> 24) & 0xFF) * adjusted_idirz + origz;
					for (int i = 0; i < 4; i++)
					{
						// Use VMIN, VMAX to compute the slabs
						const float cmin = tinybvh_max( tinybvh_max( tinybvh_max( tminx[i], tminy[i] ), tminz[i] ), tmin );
						const float cmax = tinybvh_min( tinybvh_min( tinybvh_min( tmaxx[i], tmaxy[i] ), tmaxz[i] ), tmax );
						if (cmin <= cmax) hitmask |= extract_byte( child_bits4, i ) << extract_byte( bit_index4, i );
					}
				}
				{	// Second 4
					const unsigned meta4 = *(unsigned*)&n1.w, is_inner4 = (meta4 & (meta4 << 1)) & 0x10101010;
					const unsigned inner_mask4 = sign_extend_s8x4( is_inner4 << 3 );
					const unsigned bit_index4 = (meta4 ^ (octinv & inner_mask4)) & 0x1F1F1F1F;
					const unsigned child_bits4 = (meta4 >> 5) & 0x07070707;
					unsigned swizzledLox = (ray.rD.x < 0) ? *(unsigned*)&n3.w : *(unsigned*)&n2.y, swizzledHix = (ray.rD.x < 0) ? *(unsigned*)&n2.y : *(unsigned*)&n3.w;
					unsigned swizzledLoy = (ray.rD.y < 0) ? *(unsigned*)&n4.y : *(unsigned*)&n2.w, swizzledHiy = (ray.rD.y < 0) ? *(unsigned*)&n2.w : *(unsigned*)&n4.y;
					unsigned swizzledLoz = (ray.rD.z < 0) ? *(unsigned*)&n4.w : *(unsigned*)&n3.y, swizzledHiz = (ray.rD.z < 0) ? *(unsigned*)&n3.y : *(unsigned*)&n4.w;
					float tminx[4], tminy[4], tminz[4], tmaxx[4], tmaxy[4], tmaxz[4];
					tminx[0] = ((swizzledLox >> 0) & 0xFF) * adjusted_idirx + origx, tminx[1] = ((swizzledLox >> 8) & 0xFF) * adjusted_idirx + origx, tminx[2] = ((swizzledLox >> 16) & 0xFF) * adjusted_idirx + origx;
					tminx[3] = ((swizzledLox >> 24) & 0xFF) * adjusted_idirx + origx, tminy[0] = ((swizzledLoy >> 0) & 0xFF) * adjusted_idiry + origy, tminy[1] = ((swizzledLoy >> 8) & 0xFF) * adjusted_idiry + origy;
					tminy[2] = ((swizzledLoy >> 16) & 0xFF) * adjusted_idiry + origy, tminy[3] = ((swizzledLoy >> 24) & 0xFF) * adjusted_idiry + origy, tminz[0] = ((swizzledLoz >> 0) & 0xFF) * adjusted_idirz + origz;
					tminz[1] = ((swizzledLoz >> 8) & 0xFF) * adjusted_idirz + origz, tminz[2] = ((swizzledLoz >> 16) & 0xFF) * adjusted_idirz + origz, tminz[3] = ((swizzledLoz >> 24) & 0xFF) * adjusted_idirz + origz;
					tmaxx[0] = ((swizzledHix >> 0) & 0xFF) * adjusted_idirx + origx, tmaxx[1] = ((swizzledHix >> 8) & 0xFF) * adjusted_idirx + origx, tmaxx[2] = ((swizzledHix >> 16) & 0xFF) * adjusted_idirx + origx;
					tmaxx[3] = ((swizzledHix >> 24) & 0xFF) * adjusted_idirx + origx, tmaxy[0] = ((swizzledHiy >> 0) & 0xFF) * adjusted_idiry + origy, tmaxy[1] = ((swizzledHiy >> 8) & 0xFF) * adjusted_idiry + origy;
					tmaxy[2] = ((swizzledHiy >> 16) & 0xFF) * adjusted_idiry + origy, tmaxy[3] = ((swizzledHiy >> 24) & 0xFF) * adjusted_idiry + origy, tmaxz[0] = ((swizzledHiz >> 0) & 0xFF) * adjusted_idirz + origz;
					tmaxz[1] = ((swizzledHiz >> 8) & 0xFF) * adjusted_idirz + origz, tmaxz[2] = ((swizzledHiz >> 16) & 0xFF) * adjusted_idirz + origz, tmaxz[3] = ((swizzledHiz >> 24) & 0xFF) * adjusted_idirz + origz;
					for (int i = 0; i < 4; i++)
					{
						const float cmin = tinybvh_max( tinybvh_max( tinybvh_max( tminx[i], tminy[i] ), tminz[i] ), tmin );
						const float cmax = tinybvh_min( tinybvh_min( tinybvh_min( tmaxx[i], tmaxy[i] ), tmaxz[i] ), tmax );
						if (cmin <= cmax) hitmask |= extract_byte( child_bits4, i ) << extract_byte( bit_index4, i );
					}
				}
				ngroup.y = (hitmask & 0xFF000000) | (as_uint( n0.w ) >> 24), tgroup.y = hitmask & 0x00FFFFFF;
			#ifdef __GNUC__
			#pragma GCC diagnostic pop
			#endif
			}
		}
		else tgroup = ngroup, ngroup = bvhuint2( 0 );
		while (tgroup.y != 0)
		{
			int triangleIndex = __bfind( tgroup.y );
			int triAddr = tgroup.x + triangleIndex * 3;
			const bvhvec3 v0 = blasTris[triAddr];
			const bvhvec3 edge1 = bvhvec3( blasTris[triAddr + 1] ) - v0;
			const bvhvec3 edge2 = bvhvec3( blasTris[triAddr + 2] ) - v0;
			const bvhvec3 h = cross( ray.D, edge2 );
			const float a = dot( edge1, h );
			if (fabs( a ) > 0.0000001f)
			{
				const float f = 1 / a;
				const bvhvec3 s = ray.O - v0;
				const float u = f * dot( s, h );
				if (u >= 0 && u <= 1)
				{
					const bvhvec3 q = cross( s, edge1 );
					const float v = f * dot( ray.D, q );
					if (v >= 0 && u + v <= 1)
					{
						const float d = f * dot( edge2, q );
						if (d > 0.0f && d < tmax)
						{
							triangleuv = bvhvec2( u, v ), tmax = d;
							hitAddr = as_uint( blasTris[triAddr].w );
						}
					}
				}
			}
			tgroup.y -= 1 << triangleIndex;
		}
		if (ngroup.y <= 0x00FFFFFF)
		{
			if (stackPtr > 0) { STACK_POP( /* nodeGroup */ ); }
			else
			{
				ray.hit.t = tmax;
				if (tmax < 1e30f)
					ray.hit.u = triangleuv.x, ray.hit.v = triangleuv.y;
				ray.hit.prim = hitAddr;
				break;
			}
		}
	} while (true);
	return 0;
}

#endif // BVH_USEAVX

// ============================================================================
//
//        I M P L E M E N T A T I O N  -  A R M / N E O N  C O D E
// 
// ============================================================================

#ifdef BVH_USENEON

#define ILANE(a,b) vgetq_lane_s32(a, b)

inline float32x4x2_t vmaxq_f32x2( float32x4x2_t a, float32x4x2_t b )
{
	float32x4x2_t ret;
	ret.val[0] = vmaxq_f32( a.val[0], b.val[0] );
	ret.val[1] = vmaxq_f32( a.val[1], b.val[1] );
	return ret;
}
inline float halfArea( const float32x4_t a /* a contains extent of aabb */ )
{
	ALIGNED( 64 ) float v[4];
	vst1q_f32( v, a );
	return v[0] * v[1] + v[1] * v[2] + v[2] * v[3];
}
inline float halfArea( const float32x4x2_t& a /* a contains aabb itself, with min.xyz negated */ )
{
	ALIGNED( 64 ) float c[8];
	vst1q_f32( c, a.val[0] );
	vst1q_f32( c + 4, a.val[1] );

	float ex = c[4] + c[0], ey = c[5] + c[1], ez = c[6] + c[2];
	return ex * ey + ey * ez + ez * ex;
}
#define PROCESS_PLANE( a, pos, ANLR, lN, rN, lb, rb ) if (lN * rN != 0) { \
	ANLR = halfArea( lb ) * (float)lN + halfArea( rb ) * (float)rN; \
	const float C = C_TRAV + C_INT * rSAV * ANLR; if (C < splitCost) \
	splitCost = C, bestAxis = a, bestPos = pos, bestLBox = lb, bestRBox = rb; }

void BVH::BuildNEON( const bvhvec4* vertices, const unsigned primCount )
{
	int test = BVHBINS;
	if (test != 8) assert( false ); // AVX builders require BVHBINS == 8.
	assert( ((long long)vertices & 63) == 0 ); // buffer must be cacheline-aligned
	// aligned data
	ALIGNED( 64 ) float32x4x2_t binbox[3 * BVHBINS];		// 768 bytes
	ALIGNED( 64 ) float32x4x2_t binboxOrig[3 * BVHBINS];	// 768 bytes
	ALIGNED( 64 ) unsigned count[3][BVHBINS]{};				// 96 bytes
	ALIGNED( 64 ) float32x4x2_t bestLBox, bestRBox;			// 64 bytes
	// some constants
	static const float32x4_t max4 = vdupq_n_f32( -1e30f ), half4 = vdupq_n_f32( 0.5f );
	static const float32x4_t two4 = vdupq_n_f32( 2.0f ), min1 = vdupq_n_f32( -1 );
	static const float32x4x2_t max8 = { max4, max4 };
	static const float32x4_t signFlip4 = SIMD_SETRVEC( -0.0f, -0.0f, -0.0f, 0.0f );
	static const float32x4x2_t signFlip8 = { signFlip4, vdupq_n_f32( 0 ) }; // TODO: Check me
	static const float32x4_t mask3 = vceqq_f32( SIMD_SETRVEC( 0, 0, 0, 1 ), vdupq_n_f32( 0 ) );
	static const float32x4_t binmul3 = vdupq_n_f32( BVHBINS * 0.49999f );
	for (unsigned i = 0; i < 3 * BVHBINS; i++) binboxOrig[i] = max8; // binbox initialization template
	// reset node pool
	const unsigned spaceNeeded = primCount * 2;
	if (allocatedBVHNodes < spaceNeeded)
	{
		AlignedFree( bvhNode );
		AlignedFree( triIdx );
		AlignedFree( fragment );
		verts = (bvhvec4*)vertices;
		triIdx = (unsigned*)AlignedAlloc( primCount * sizeof( unsigned ) );
		bvhNode = (BVHNode*)AlignedAlloc( spaceNeeded * sizeof( BVHNode ) );
		allocatedBVHNodes = spaceNeeded;
		memset( &bvhNode[1], 0, 32 ); // avoid crash in refit.
		fragment = (Fragment*)AlignedAlloc( primCount * sizeof( Fragment ) );
	}
	else FATAL_ERROR_IF( !rebuildable, "BVH::BuildNEON( .. ), bvh not rebuildable." );
	triCount = idxCount = primCount;
	unsigned newNodePtr = 2;
	struct FragSSE { float32x4_t bmin4, bmax4; };
	FragSSE* frag4 = (FragSSE*)fragment;
	float32x4x2_t* frag8 = (float32x4x2_t*)fragment;
	const float32x4_t* verts4 = (float32x4_t*)verts;
	// assign all triangles to the root node
	BVHNode& root = bvhNode[0];
	root.leftFirst = 0, root.triCount = triCount;
	// initialize fragments and update root bounds
	float32x4_t rootMin = max4, rootMax = max4;
	for (unsigned i = 0; i < triCount; i++)
	{
		const float32x4_t v1 = veorq_s32( signFlip4, vminq_f32( vminq_f32( verts4[i * 3], verts4[i * 3 + 1] ), verts4[i * 3 + 2] ) );
		const float32x4_t v2 = vmaxq_f32( vmaxq_f32( verts4[i * 3], verts4[i * 3 + 1] ), verts4[i * 3 + 2] );
		frag4[i].bmin4 = v1, frag4[i].bmax4 = v2, rootMin = vmaxq_f32( rootMin, v1 ), rootMax = vmaxq_f32( rootMax, v2 ), triIdx[i] = i;
	}
	rootMin = veorq_s32( rootMin, signFlip4 );
	root.aabbMin = *(bvhvec3*)&rootMin, root.aabbMax = *(bvhvec3*)&rootMax;
	// subdivide recursively
	ALIGNED( 64 ) unsigned task[128], taskCount = 0, nodeIdx = 0;
	const bvhvec3 minDim = (root.aabbMax - root.aabbMin) * 1e-7f;
	while (1)
	{
		while (1)
		{
			BVHNode& node = bvhNode[nodeIdx];
			float32x4_t* node4 = (float32x4_t*)&bvhNode[nodeIdx];
			// find optimal object split
			const float32x4_t d4 = vbslq_f32( vshrq_n_s32( mask3, 31 ), vsubq_f32( node4[1], node4[0] ), min1 );
			const float32x4_t nmin4 = vmulq_f32( vandq_s32( node4[0], mask3 ), two4 );
			const float32x4_t rpd4 = vandq_s32( vdivq_f32( binmul3, d4 ), vmvnq_u32( vceqq_f32( d4, vdupq_n_f32( 0 ) ) ) );
			// implementation of Section 4.1 of "Parallel Spatial Splits in Bounding Volume Hierarchies":
			// main loop operates on two fragments to minimize dependencies and maximize ILP.
			unsigned fi = triIdx[node.leftFirst];
			memset( count, 0, sizeof( count ) );
			float32x4x2_t r0, r1, r2, f = frag8[fi];
			int32x4_t bi4 = vcvtq_s32_f32( vrnd32xq_f32( vsubq_f32( vmulq_f32( vsubq_f32( vsubq_f32( frag4[fi].bmax4, frag4[fi].bmin4 ), nmin4 ), rpd4 ), half4 ) ) );
			memcpy( binbox, binboxOrig, sizeof( binbox ) );
			unsigned i0 = ILANE( bi4, 0 ), i1 = ILANE( bi4, 1 ), i2 = ILANE( bi4, 2 ), * ti = triIdx + node.leftFirst + 1;
			for (unsigned i = 0; i < node.triCount - 1; i++)
			{
				unsigned fid = *ti++;
				const float32x4x2_t b0 = binbox[i0];
				const float32x4x2_t b1 = binbox[BVHBINS + i1];
				const float32x4x2_t b2 = binbox[2 * BVHBINS + i2];
				const float32x4_t fmin = frag4[fid].bmin4, fmax = frag4[fid].bmax4;
				r0 = vmaxq_f32x2( b0, f );
				r1 = vmaxq_f32x2( b1, f );
				r2 = vmaxq_f32x2( b2, f );
				const int32x4_t b4 = vcvtq_s32_f32( vrnd32xq_f32( vsubq_f32( vmulq_f32( vsubq_f32( vsubq_f32( fmax, fmin ), nmin4 ), rpd4 ), half4 ) ) );

				f = frag8[fid], count[0][i0]++, count[1][i1]++, count[2][i2]++;
				binbox[i0] = r0, i0 = ILANE( b4, 0 );
				binbox[BVHBINS + i1] = r1, i1 = ILANE( b4, 1 );
				binbox[2 * BVHBINS + i2] = r2, i2 = ILANE( b4, 2 );
			}
			// final business for final fragment
			const float32x4x2_t b0 = binbox[i0], b1 = binbox[BVHBINS + i1], b2 = binbox[2 * BVHBINS + i2];
			count[0][i0]++, count[1][i1]++, count[2][i2]++;
			r0 = vmaxq_f32x2( b0, f );
			r1 = vmaxq_f32x2( b1, f );
			r2 = vmaxq_f32x2( b2, f );
			binbox[i0] = r0, binbox[BVHBINS + i1] = r1, binbox[2 * BVHBINS + i2] = r2;
			// calculate per-split totals
			float splitCost = 1e30f, rSAV = 1.0f / node.SurfaceArea();
			unsigned bestAxis = 0, bestPos = 0, n = newNodePtr, j = node.leftFirst + node.triCount, src = node.leftFirst;
			const float32x4x2_t* bb = binbox;
			for (int a = 0; a < 3; a++, bb += BVHBINS) if ((node.aabbMax[a] - node.aabbMin[a]) > minDim.cell[a])
			{
				// hardcoded bin processing for BVHBINS == 8
				assert( BVHBINS == 8 );
				const unsigned lN0 = count[a][0], rN0 = count[a][7];
				const float32x4x2_t lb0 = bb[0], rb0 = bb[7];
				const unsigned lN1 = lN0 + count[a][1], rN1 = rN0 + count[a][6], lN2 = lN1 + count[a][2];
				const unsigned rN2 = rN1 + count[a][5], lN3 = lN2 + count[a][3], rN3 = rN2 + count[a][4];
				const float32x4x2_t lb1 = vmaxq_f32x2( lb0, bb[1] ), rb1 = vmaxq_f32x2( rb0, bb[6] );
				const float32x4x2_t lb2 = vmaxq_f32x2( lb1, bb[2] ), rb2 = vmaxq_f32x2( rb1, bb[5] );
				const float32x4x2_t lb3 = vmaxq_f32x2( lb2, bb[3] ), rb3 = vmaxq_f32x2( rb2, bb[4] );
				const unsigned lN4 = lN3 + count[a][4], rN4 = rN3 + count[a][3], lN5 = lN4 + count[a][5];
				const unsigned rN5 = rN4 + count[a][2], lN6 = lN5 + count[a][6], rN6 = rN5 + count[a][1];
				const float32x4x2_t lb4 = vmaxq_f32x2( lb3, bb[4] ), rb4 = vmaxq_f32x2( rb3, bb[3] );
				const float32x4x2_t lb5 = vmaxq_f32x2( lb4, bb[5] ), rb5 = vmaxq_f32x2( rb4, bb[2] );
				const float32x4x2_t lb6 = vmaxq_f32x2( lb5, bb[6] ), rb6 = vmaxq_f32x2( rb5, bb[1] );
				float ANLR3 = 1e30f; PROCESS_PLANE( a, 3, ANLR3, lN3, rN3, lb3, rb3 ); // most likely split
				float ANLR2 = 1e30f; PROCESS_PLANE( a, 2, ANLR2, lN2, rN4, lb2, rb4 );
				float ANLR4 = 1e30f; PROCESS_PLANE( a, 4, ANLR4, lN4, rN2, lb4, rb2 );
				float ANLR5 = 1e30f; PROCESS_PLANE( a, 5, ANLR5, lN5, rN1, lb5, rb1 );
				float ANLR1 = 1e30f; PROCESS_PLANE( a, 1, ANLR1, lN1, rN5, lb1, rb5 );
				float ANLR0 = 1e30f; PROCESS_PLANE( a, 0, ANLR0, lN0, rN6, lb0, rb6 );
				float ANLR6 = 1e30f; PROCESS_PLANE( a, 6, ANLR6, lN6, rN0, lb6, rb0 ); // least likely split
			}
			float noSplitCost = (float)node.triCount * C_INT;
			if (splitCost >= noSplitCost) break; // not splitting is better.
			// in-place partition
			const float rpd = (*(bvhvec3*)&rpd4)[bestAxis], nmin = (*(bvhvec3*)&nmin4)[bestAxis];
			unsigned t, fr = triIdx[src];
			for (unsigned i = 0; i < node.triCount; i++)
			{
				const unsigned bi = (unsigned)((fragment[fr].bmax[bestAxis] - fragment[fr].bmin[bestAxis] - nmin) * rpd);
				if (bi <= bestPos) fr = triIdx[++src]; else t = fr, fr = triIdx[src] = triIdx[--j], triIdx[j] = t;
			}
			// create child nodes and recurse
			const unsigned leftCount = src - node.leftFirst, rightCount = node.triCount - leftCount;
			if (leftCount == 0 || rightCount == 0) break; // should not happen.
			(*(float32x4x2_t*)&bvhNode[n]).val[0] = veorq_s32( bestLBox.val[0], signFlip8.val[0] );
			(*(float32x4x2_t*)&bvhNode[n]).val[1] = veorq_s32( bestLBox.val[1], signFlip8.val[1] );
			bvhNode[n].leftFirst = node.leftFirst, bvhNode[n].triCount = leftCount;
			node.leftFirst = n++, node.triCount = 0, newNodePtr += 2;
			(*(float32x4x2_t*)&bvhNode[n]).val[0] = veorq_s32( bestRBox.val[0], signFlip8.val[0] );
			(*(float32x4x2_t*)&bvhNode[n]).val[1] = veorq_s32( bestRBox.val[1], signFlip8.val[1] );
			bvhNode[n].leftFirst = j, bvhNode[n].triCount = rightCount;
			task[taskCount++] = n, nodeIdx = n - 1;
		}
		// fetch subdivision task from stack
		if (taskCount == 0) break; else nodeIdx = task[--taskCount];
	}
	// all done.
	refittable = true; // not using spatial splits: can refit this BVH
	frag_min_flipped = true; // NEON was used for binning; fragment.min flipped
	may_have_holes = false; // the NEON builder produces a continuous list of nodes
	usedBVHNodes = newNodePtr;
}

// Traverse the second alternative BVH layout (ALT_SOA).
int BVH::Intersect_AltSoA( Ray& ray ) const
{
	BVHNodeAlt2* node = &alt2Node[0], * stack[64];
	unsigned stackPtr = 0, steps = 0;
	const float32x4_t Ox4 = vdupq_n_f32( ray.O.x ), rDx4 = vdupq_n_f32( ray.rD.x );
	const float32x4_t Oy4 = vdupq_n_f32( ray.O.y ), rDy4 = vdupq_n_f32( ray.rD.y );
	const float32x4_t Oz4 = vdupq_n_f32( ray.O.z ), rDz4 = vdupq_n_f32( ray.rD.z );
	// const float32x4_t inf4 = vdupq_n_f32( 1e30f );
	while (1)
	{
		steps++;
		if (node->isLeaf())
		{
			for (unsigned i = 0; i < node->triCount; i++)
			{
				const unsigned tidx = triIdx[node->firstTri + i], vertIdx = tidx * 3;
				const bvhvec3 edge1 = verts[vertIdx + 1] - verts[vertIdx];
				const bvhvec3 edge2 = verts[vertIdx + 2] - verts[vertIdx];
				const bvhvec3 h = cross( ray.D, edge2 );
				const float a = dot( edge1, h );
				if (fabs( a ) < 0.0000001f) continue; // ray parallel to triangle
				const float f = 1 / a;
				const bvhvec3 s = ray.O - bvhvec3( verts[vertIdx] );
				const float u = f * dot( s, h );
				if (u < 0 || u > 1) continue;
				const bvhvec3 q = cross( s, edge1 );
				const float v = f * dot( ray.D, q );
				if (v < 0 || u + v > 1) continue;
				const float t = f * dot( edge2, q );
				if (t < 0 || t > ray.hit.t) continue;
				ray.hit.t = t, ray.hit.u = u, ray.hit.v = v, ray.hit.prim = tidx;
			}
			if (stackPtr == 0) break; else node = stack[--stackPtr];
			continue;
		}
		float32x4_t x4 = vmulq_f32( vsubq_f32( node->xxxx, Ox4 ), rDx4 );
		float32x4_t y4 = vmulq_f32( vsubq_f32( node->yyyy, Oy4 ), rDy4 );
		float32x4_t z4 = vmulq_f32( vsubq_f32( node->zzzz, Oz4 ), rDz4 );
		// transpose
		float32x4_t t0 = vzip1q_f32( x4, y4 ), t2 = vzip1q_f32( z4, z4 );
		float32x4_t t1 = vzip2q_f32( x4, y4 ), t3 = vzip2q_f32( z4, z4 );
		float32x4_t xyzw1a = vcombine_f32( vget_low_f32( t0 ), vget_low_f32( t2 ) );
		float32x4_t xyzw2a = vcombine_f32( vget_high_f32( t0 ), vget_high_f32( t2 ) );
		float32x4_t xyzw1b = vcombine_f32( vget_low_f32( t1 ), vget_low_f32( t3 ) );
		float32x4_t xyzw2b = vcombine_f32( vget_high_f32( t1 ), vget_high_f32( t3 ) );
		// process
		float32x4_t tmina4 = vminq_f32( xyzw1a, xyzw2a ), tmaxa4 = vmaxq_f32( xyzw1a, xyzw2a );
		float32x4_t tminb4 = vminq_f32( xyzw1b, xyzw2b ), tmaxb4 = vmaxq_f32( xyzw1b, xyzw2b );
		// transpose back
		t0 = vzip1q_f32( tmina4, tmaxa4 ), t2 = vzip1q_f32( tminb4, tmaxb4 );
		t1 = vzip2q_f32( tmina4, tmaxa4 ), t3 = vzip2q_f32( tminb4, tmaxb4 );
		x4 = vcombine_f32( vget_low_f32( t0 ), vget_low_f32( t2 ) );
		y4 = vcombine_f32( vget_high_f32( t0 ), vget_high_f32( t2 ) );
		z4 = vcombine_f32( vget_low_f32( t1 ), vget_low_f32( t3 ) );
		unsigned lidx = node->left, ridx = node->right;
		const float32x4_t min4 = vmaxq_f32( vmaxq_f32( vmaxq_f32( x4, y4 ), z4 ), vdupq_n_f32( 0 ) );
		const float32x4_t max4 = vminq_f32( vminq_f32( vminq_f32( x4, y4 ), z4 ), vdupq_n_f32( ray.hit.t ) );
	#if 0
		// TODO: why is this slower on gen14?
		const float tmina_0 = vgetq_lane_f32( min4, 0 ), tmaxa_1 = vgetq_lane_f32( max4, 1 );
		const float tminb_2 = vgetq_lane_f32( min4, 2 ), tmaxb_3 = vgetq_lane_f32( max4, 3 );
		t0 = __builtin_shufflevector( max4, max4, 3, 1, 3, 1 );
		t1 = __builtin_shufflevector( min4, min4, 2, 0, 2, 0 );
		t0 = vbslq_f32( vcgeq_f32( t0, t1 ), t1, inf4 );
		float dist1 = vgetq_lane_f32( t0, 1 ), dist2 = vgetq_lane_f32( t0, 0 );
	#else
		const float tmina_0 = vgetq_lane_f32( min4, 0 ), tmaxa_1 = vgetq_lane_f32( max4, 1 );
		const float tminb_2 = vgetq_lane_f32( min4, 2 ), tmaxb_3 = vgetq_lane_f32( max4, 3 );
		float dist1 = tmaxa_1 >= tmina_0 ? tmina_0 : 1e30f;
		float dist2 = tmaxb_3 >= tminb_2 ? tminb_2 : 1e30f;
	#endif
		if (dist1 > dist2)
		{
			float t = dist1; dist1 = dist2; dist2 = t;
			unsigned i = lidx; lidx = ridx; ridx = i;
		}
		if (dist1 == 1e30f)
		{
			if (stackPtr == 0) break; else node = stack[--stackPtr];
		}
		else
		{
			node = alt2Node + lidx;
			if (dist2 != 1e30f) stack[stackPtr++] = alt2Node + ridx;
		}
	}
	return steps;
}

#endif // BVH_USENEON

} // namespace tinybvh

#endif // TINYBVH_IMPLEMENTATION

#endif // TINY_BVH_H_