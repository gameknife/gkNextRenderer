#pragma once

#include "Common/CoreMinimal.hpp"

#if WIN32
#define GK_PLUGIN_EXPORT __declspec(dllexport)
#else
#define GK_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

inline constexpr uint32_t GK_ENGINE_ABI_VERSION = 20260507u;
