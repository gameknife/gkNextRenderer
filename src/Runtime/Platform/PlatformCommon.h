#pragma once

#if ANDROID
#include "PlatformAndroid.h"
#elif WIN32
#include "PlatformWindows.h"
#else
#include "PlatformLinux.h"
#endif