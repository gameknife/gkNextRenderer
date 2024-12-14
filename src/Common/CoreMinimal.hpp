#pragma once

#include <cstring>
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <memory>
#include <functional>
#include <fstream>
#include <fmt/printf.h>

#if WIN32 && !defined(__MINGW32__)
#define DISABLE_OPTIMIZATION __pragma(optimize("", off))
#define ENABLE_OPTIMIZATION __pragma(optimize("", on))
#else
#define DISABLE_OPTIMIZATION
#define ENABLE_OPTIMIZATION
#endif

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