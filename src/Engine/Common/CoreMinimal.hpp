#pragma once

// Core minimal includes for gkNextRenderer project
// This file should be included in all source files that need common functionality

// Standard library includes
#include <cstring>
#include <string>
#include <vector>
#include <array>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <functional>
#include <fstream>
#include <set>
#include <iostream>
#include <filesystem>
#include <chrono>
#include <cstdint>

// fmt library (still needed for formatting)
#include <fmt/printf.h>
#include <fmt/format.h>
#include <fmt/chrono.h>

// spdlog logging (used pervasively across the engine)
#include <spdlog/spdlog.h>

#if WIN32
#define DISABLE_OPTIMIZATION __pragma(optimize("", off))
#define ENABLE_OPTIMIZATION __pragma(optimize("", on))
#else
#define DISABLE_OPTIMIZATION
#define ENABLE_OPTIMIZATION
#endif

#define GK_NON_COPIABLE(ClassName) \
	ClassName(const ClassName&) = delete; \
	ClassName(ClassName&&) = delete; \
	ClassName& operator = (const ClassName&) = delete; \
	ClassName& operator = (ClassName&&) = delete;

#ifdef ENGINE_API_SHARED

#if defined(_WIN32) || defined(_WIN64)

  #ifdef ENGINE_EXPORTS
    #define ENGINE_API __declspec(dllexport)
  #else
    #define ENGINE_API __declspec(dllimport)
  #endif
#else
  #ifdef ENGINE_EXPORTS
    #define ENGINE_API __attribute__((visibility("default")))
  #else
    #define ENGINE_API
  #endif
#endif

#else

  #define ENGINE_API

#endif

#if WITH_SUPERLUMINAL
#include "Superluminal/PerformanceAPI.h"

namespace GkPerformance
{
	inline uint32_t HashInstrumentationName(const char* name)
	{
		uint32_t hash = 2166136261u;
		if (name == nullptr)
		{
			return hash;
		}

		while (*name != '\0')
		{
			hash ^= static_cast<uint8_t>(*name);
			hash *= 16777619u;
			++name;
		}
		return hash;
	}

	inline uint32_t HashInstrumentationName(const wchar_t* name)
	{
		uint32_t hash = 2166136261u;
		if (name == nullptr)
		{
			return hash;
		}

		while (*name != L'\0')
		{
			const auto value = static_cast<uint32_t>(*name);
			hash ^= value & 0xffu;
			hash *= 16777619u;
			hash ^= (value >> 8u) & 0xffu;
			hash *= 16777619u;
			++name;
		}
		return hash;
	}

	inline uint32_t MakeInstrumentationColor(uint32_t hash)
	{
		hash ^= hash >> 16u;
		hash *= 0x7feb352du;
		hash ^= hash >> 15u;
		hash *= 0x846ca68bu;
		hash ^= hash >> 16u;

		const uint32_t red = 80u + (hash & 0x7fu);
		const uint32_t green = 80u + ((hash >> 8u) & 0x7fu);
		const uint32_t blue = 80u + ((hash >> 16u) & 0x7fu);
		return PERFORMANCEAPI_MAKE_COLOR(red, green, blue);
	}

	inline uint32_t MakeInstrumentationColor(const char* name)
	{
		return MakeInstrumentationColor(HashInstrumentationName(name));
	}

	inline uint32_t MakeInstrumentationColor(const wchar_t* name)
	{
		return MakeInstrumentationColor(HashInstrumentationName(name));
	}
}

#undef PERFORMANCEAPI_INSTRUMENT_DATA
#define PERFORMANCEAPI_INSTRUMENT_DATA(InstrumentationID, InstrumentationData) PERFORMANCEAPI_INSTRUMENT_DATA_COLOR((InstrumentationID), (InstrumentationData), GkPerformance::MakeInstrumentationColor((InstrumentationID)))
#else
#define PERFORMANCEAPI_INSTRUMENT(InstrumentationID)
#define PERFORMANCEAPI_INSTRUMENT_DATA(InstrumentationID, InstrumentationData)
#define PERFORMANCEAPI_INSTRUMENT_COLOR(InstrumentationID, InstrumentationColor)
#define PERFORMANCEAPI_INSTRUMENT_DATA_COLOR(InstrumentationID, InstrumentationData, InstrumentationColor)

#define PERFORMANCEAPI_INSTRUMENT_FUNCTION()
#define PERFORMANCEAPI_INSTRUMENT_FUNCTION_DATA(InstrumentationData)
#define PERFORMANCEAPI_INSTRUMENT_FUNCTION_COLOR(InstrumentationColor)
#define PERFORMANCEAPI_INSTRUMENT_FUNCTION_DATA_COLOR(InstrumentationData, InstrumentationColor)

inline void SetCurrentThreadName(const char* inThreadName) {}
inline void BeginEvent(const char* inID) {}
inline void BeginEvent(const char* inID, const char* inData) {}
inline void BeginEvent(const char* inID, const char* inData, uint32_t inColor) {}
inline void BeginEvent(const wchar_t* inID) {}
inline void BeginEvent(const wchar_t* inID, const wchar_t* inData) {}
inline void BeginEvent(const wchar_t* inID, const wchar_t* inData, uint32_t inColor) {}
inline void EndEvent() {}
#endif
