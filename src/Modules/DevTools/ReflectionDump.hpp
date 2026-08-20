#pragma once

// Build-time reflection manifest export; not required by the runtime core.
#include <string>

namespace Reflection
{
    /// Writes the reflection manifest that generates the managed component wrappers.
    ///
    /// The engine is the only thing that knows what entt::meta actually holds, so it is the only
    /// thing that can produce this file. The result is committed
    /// (src/Modules/NextDotNet/ReflectionManifest.json) and `gnb csharpgen` reads the committed
    /// copy rather than running the engine: code generation must not require a built binary, or
    /// `gnb csharpgen --check` could not run before the build it is meant to guard. A unit test
    /// compares the committed manifest against live reflection so the snapshot cannot go stale
    /// unnoticed.
    ///
    /// Returns false and logs the reason if the file could not be written.
    bool DumpManifest(const std::string& outputPath);
}
