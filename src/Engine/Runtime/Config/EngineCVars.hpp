#pragma once

#include "Engine/Runtime/RuntimeFwd.hpp"

namespace NextCVar
{
    void RegisterEngineCVars(FCVarSystem& cvars, Runtime::Config::UserSettings& settings, Runtime::Config::ShowFlags& showFlags, NextEngine* engine);
}
