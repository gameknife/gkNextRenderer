#include "Modules/NextDotNet/Interop.h"

// Stand-in for the managed entry point, linked into executables that use the NextDotNet module but
// host no C# themselves — the unit tests, for instance, which exercise the native binding table
// and never start a runtime.
//
// Only needed under NativeAOT: there GkNext_Bootstrap is an ordinary symbol that the linker
// demands, and without a game there is nothing to provide it. Under CoreCLR the symbol is resolved
// at runtime through hostfxr, so nothing references it at link time.
//
// Returning a non-zero status makes the absence explicit: a host that tries to start managed code
// in such a binary reports a clean failure instead of crashing on a null table.

extern "C" int32_t GkNext_Bootstrap(const Modules::NextDotNet::FEngineApi* engineApi,
                                    Modules::NextDotNet::FManagedApi* outManagedApi)
{
    (void)engineApi;
    (void)outManagedApi;
    return -1;
}
