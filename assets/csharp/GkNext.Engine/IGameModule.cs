using GkNext.Interop;

namespace GkNext;

/// <summary>
/// The seam between the never-unloaded bootstrap assembly and the reloadable game assembly.
/// </summary>
/// <remarks>
/// This type lives in GkNext.Engine, which always resolves from the default AssemblyLoadContext,
/// so an instance created inside a collectible ALC still casts to this interface across the
/// boundary. Phase 4 puts a source-generated registry behind <c>GkNext.Game.Entry</c>; the shape
/// of this interface is what both backends agree on.
/// </remarks>
public interface IGameModule
{
    /// <summary>Called once after the module is loaded, before the first Tick.</summary>
    void Initialize();

    /// <summary>Called once per frame.</summary>
    void Tick(double deltaSeconds);

    /// <summary>
    /// Raised for engine lifecycle events. Return true to report the hook as consumed; only
    /// <see cref="ScriptHook.OnRenderUI"/> acts on it, matching the QuickJS onRenderUI contract.
    /// </summary>
    bool Lifecycle(ScriptHook hook, double deltaSeconds);

    /// <summary>Raised for each input event. Return true to stop the host handling it.</summary>
    bool OnInputEvent(in InputEvent inputEvent);

    /// <summary>
    /// Fill <paramref name="camera"/> and return true to drive the render camera this frame.
    /// </summary>
    bool OverrideCamera(ref CameraOverride camera);

    /// <summary>Called before the module is unloaded, including on hot reload.</summary>
    void Shutdown();
}

/// <summary>
/// Default implementations of everything a game does not care about, so a game module only
/// overrides what it uses. Phase 4's NextGameInstance builds on this.
/// </summary>
public abstract class GameModuleBase : IGameModule
{
    public virtual void Initialize() { }

    public virtual void Tick(double deltaSeconds) { }

    public virtual bool Lifecycle(ScriptHook hook, double deltaSeconds) => false;

    public virtual bool OnInputEvent(in InputEvent inputEvent) => false;

    public virtual bool OverrideCamera(ref CameraOverride camera) => false;

    public virtual void Shutdown() { }
}
