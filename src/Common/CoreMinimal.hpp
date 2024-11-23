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