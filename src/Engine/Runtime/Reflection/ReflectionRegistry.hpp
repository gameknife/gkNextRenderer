#pragma once

namespace Reflection
{
    // Register all reflection metadata
    // Must be called once at engine initialization
    void RegisterAllReflection();
    
    // Check if reflection has been initialized
    bool IsReflectionInitialized();
}
