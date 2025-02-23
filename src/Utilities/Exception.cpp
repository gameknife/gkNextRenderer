#include "Exception.hpp"
#if !ANDROID
#include <cpptrace/cpptrace.hpp>
#endif

#undef APIENTRY

namespace NextStackWalk
{
    void PrintStack()
    {
#if !ANDROID
        cpptrace::generate_trace().print();
#endif
    }
}