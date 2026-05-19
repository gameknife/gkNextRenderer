#pragma once
#include <stdexcept>

namespace NextStackWalk
{
    void PrintStack();
}

template <class E>
[[noreturn]] void Throw(const E& e) noexcept(false)
{
    //SPDLOG_ERROR("\nException: {}\n------------------", e.what());
    NextStackWalk::PrintStack();
    throw e;
}