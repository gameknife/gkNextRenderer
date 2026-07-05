#include "Engine/Utilities/Exception.hpp"
#if !ANDROID && !IOS
#include <cpptrace/cpptrace.hpp>
#endif

#undef APIENTRY

namespace NextStackWalk
{
    void PrintStack()
    {
#if !ANDROID && !IOS
        cpptrace::generate_trace().print();
#endif
    }
}
