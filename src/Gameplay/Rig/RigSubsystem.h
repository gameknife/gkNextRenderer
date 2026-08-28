#pragma once

class NextEngine;

namespace NextGameplay::Rig
{
    /// Installs the NextRig implementation on `engine`, so ScadRig characters can be pooled,
    /// animated and — through the NextDotNet bindings — driven from C#.
    ///
    /// Call once from an application's CreateGameInstance, before the engine starts, the same way
    /// a loader module is registered. Cheap when unused: an application that never declares a pool
    /// pays for one empty object.
    ///
    /// Needs the ScadLoader module for the .scad rigs themselves; where it is absent every
    /// DeclarePool fails with a logged reason instead of the application failing to start.
    void Install(NextEngine& engine);
}
