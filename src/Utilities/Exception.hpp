#pragma once
#include <stdexcept>
#include <fmt/printf.h>
#include "Console.hpp"

namespace NextStackWalk
{
    void PrintStack();
}

template <class E>
[[noreturn]] void Throw(const E& e) noexcept(false)
{
    fmt::print("\n{}Exception: {}{}\n------------------\n", CONSOLE_RED_COLOR, e.what(), CONSOLE_DEFAULT_COLOR);
    NextStackWalk::PrintStack();
    throw e;
}