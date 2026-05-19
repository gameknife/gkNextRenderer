#pragma once

class NextEngine;

namespace Runtime::Config
{
    struct UserSettings;
    struct ShowFlags;
}

namespace NextCVar
{
    class FCVarSystem;

    void RegisterEngineCVars(FCVarSystem& cvars, Runtime::Config::UserSettings& settings, Runtime::Config::ShowFlags& showFlags, NextEngine* engine);
}
