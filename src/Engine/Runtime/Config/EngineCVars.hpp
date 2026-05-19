#pragma once

class NextEngine;
struct UserSettings;
struct ShowFlags;

namespace NextCVar
{
    class FCVarSystem;

    void RegisterEngineCVars(FCVarSystem& cvars, UserSettings& settings, ShowFlags& showFlags, NextEngine* engine);
}
