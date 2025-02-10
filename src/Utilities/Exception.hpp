#pragma once

//#include <stdexcept>
#include <cpptrace/cpptrace.hpp>
#include <fmt/printf.h>
#include "Console.hpp"
#undef APIENTRY

template <class E>
[[noreturn]] inline void Throw(const E& e) noexcept(false)
{
	fmt::print("\n{}Exception: {}{}\n------------------\n", CONSOLE_RED_COLOR, e.what(), CONSOLE_DEFAULT_COLOR);
	cpptrace::generate_trace().print();
	throw e;
}
