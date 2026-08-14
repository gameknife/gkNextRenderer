using GkNext.Interop;

namespace GkNext;

/// <summary>
/// Base class for a C# game. Replaces the QuickJS-era NextGameInstanceBase.ts.
/// </summary>
/// <remarks>
/// Translates the flat lifecycle hook the ABI carries into named overrides, so game code never
/// sees hook ids or the interop layer. Everything is virtual and does nothing by default: a game
/// overrides only what it uses.
/// </remarks>
public abstract class NextGameInstance : IGameModule
{
    private readonly FrameAllocationGuard allocationGuard = new();

    /// <summary>Called once after the module is loaded, before the first tick.</summary>
    protected virtual void OnInit() { }

    /// <summary>Called once per frame.</summary>
    /// <remarks>
    /// A game that moves nodes must call <c>Scene.MarkTransformDirty()</c> once per tick.
    /// Setting a node's transform updates the scene graph but does not tell the renderer to
    /// re-upload instance transforms, so without it everything stays where it was first built.
    /// </remarks>
    protected virtual void OnTick(double deltaSeconds) { }

    /// <summary>Draw immediate-mode UI. Return true to tell the host the frame was consumed.</summary>
    protected virtual bool OnRenderUI() => false;

    /// <summary>
    /// Build procedural scene content. The <c>SceneBuild</c> namespace is only usable inside this
    /// call; anything it is asked to do outside the window is refused by the engine.
    /// </summary>
    /// <remarks>
    /// The reverse also holds: the live <c>Scene</c> accessors do not work here. Nodes are
    /// addressable by id only once the scene is committed, so give a node its starting transform
    /// and visibility through the <see cref="Interop.RenderNodeSpec"/> it is built with rather than
    /// by calling <c>Scene.SetNode*</c> from inside this hook.
    /// </remarks>
    protected virtual void BeforeSceneRebuild() { }

    /// <summary>Called after the scene finished loading and committing.</summary>
    protected virtual void OnSceneLoaded() { }

    /// <summary>Called before the module is unloaded, including on hot reload.</summary>
    protected virtual void OnDestroy() { }

    /// <summary>Return true to stop the host from handling the event.</summary>
    protected virtual bool OnInputEvent(in InputEvent inputEvent) => false;

    /// <summary>Fill the camera and return true to drive the render camera this frame.</summary>
    protected virtual bool OnOverrideCamera(ref CameraOverride camera) => false;

    // --- IGameModule plumbing ---------------------------------------------------------------
    // Sealed: a game overrides the named hooks above, not the ABI-shaped ones.

    void IGameModule.Initialize() => OnInit();

    void IGameModule.Tick(double deltaSeconds)
    {
        allocationGuard.BeginFrame();
        OnTick(deltaSeconds);
        allocationGuard.EndFrame();
    }

    bool IGameModule.Lifecycle(ScriptHook hook, double deltaSeconds)
    {
        switch (hook)
        {
            case ScriptHook.OnInit:
                OnInit();
                return false;
            case ScriptHook.OnDestroy:
                OnDestroy();
                return false;
            case ScriptHook.BeforeSceneRebuild:
                BeforeSceneRebuild();
                return false;
            case ScriptHook.OnSceneLoaded:
                OnSceneLoaded();
                return false;
            case ScriptHook.OnRenderUI:
                return OnRenderUI();
            default:
                return false;
        }
    }

    bool IGameModule.OnInputEvent(in InputEvent inputEvent) => OnInputEvent(in inputEvent);

    bool IGameModule.OverrideCamera(ref CameraOverride camera) => OnOverrideCamera(ref camera);

    void IGameModule.Shutdown() => OnDestroy();
}

/// <summary>
/// Watches how much a frame allocates and complains once when it goes over budget.
/// </summary>
/// <remarks>
/// The engine has frame-time-sensitive paths, and managed per-frame allocation is the realistic
/// way this scripting layer would degrade them (design section 9). The guard makes that visible
/// during development instead of at the point where someone notices a stutter.
///
/// Off by default and enabled with <c>GK_DOTNET_ALLOC_GUARD=1</c>: it is a development aid, not
/// something to pay for in a shipped build. It reports once rather than every frame, because an
/// allocating tick allocates every tick and a per-frame log would itself be the problem.
/// </remarks>
internal sealed class FrameAllocationGuard
{
    private const long DefaultBudgetBytes = 4096;

    private readonly bool enabled;
    private readonly long budgetBytes;
    private long frameStartBytes;
    private bool reported;

    public FrameAllocationGuard()
    {
        enabled = Environment.GetEnvironmentVariable("GK_DOTNET_ALLOC_GUARD") == "1";
        budgetBytes = DefaultBudgetBytes;
        if (long.TryParse(Environment.GetEnvironmentVariable("GK_DOTNET_ALLOC_BUDGET"), out long configured) &&
            configured > 0)
        {
            budgetBytes = configured;
        }
    }

    public void BeginFrame()
    {
        if (!enabled || reported)
        {
            return;
        }
        frameStartBytes = GC.GetAllocatedBytesForCurrentThread();
    }

    public void EndFrame()
    {
        if (!enabled || reported)
        {
            return;
        }

        long allocated = GC.GetAllocatedBytesForCurrentThread() - frameStartBytes;
        if (allocated <= budgetBytes)
        {
            return;
        }

        reported = true;
        Log.Warn($"frame allocated {allocated} bytes, over the {budgetBytes} byte budget. " +
                 "Gameplay hot paths should avoid LINQ, closures, boxing and string interpolation; " +
                 "prefer structs, spans and pooled objects. Further frames are not reported.");
    }
}
