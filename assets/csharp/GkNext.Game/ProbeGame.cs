using GkNext.Interop;

namespace GkNext.Game;

/// <summary>
/// Acceptance payload for the two-backend probe: exercises every direction of the ABI so a failure
/// in any of them is visible in the probe's transcript rather than as a silent no-op.
/// </summary>
/// <remarks>
/// Prints its build variant on every line. Under CoreCLR the probe reloads a differently built
/// copy of this assembly and asserts the variant changes, which is what distinguishes a real
/// assembly swap from merely re-running the code already loaded.
/// </remarks>
[GameInstance]
public sealed class ProbeGame : GameModuleBase
{
    private int tickCount;

    public override void Initialize()
    {
        Log.Info($"[game {BuildInfo.Variant}] Initialize, engine frame {Engine.GetTotalFrames()}");

        // Exercise the generated wrappers that involve marshalling, so a broken arena, a wrong
        // struct layout or a bad color packing shows up here rather than in a game.
        Vector2 screen = UI.GetScreenSize();
        Log.Info($"[game {BuildInfo.Variant}] screen {screen}, white=0x{Color.White.ToPacked():X8}");
    }

    public override void Tick(double deltaSeconds)
    {
        tickCount++;
        Log.Info($"[game {BuildInfo.Variant}] Tick #{tickCount} dt={deltaSeconds:F3} frame={Engine.GetTotalFrames()} time={Engine.GetTime():F3}");
    }

    public override bool Lifecycle(ScriptHook hook, double deltaSeconds)
    {
        Log.Info($"[game {BuildInfo.Variant}] Lifecycle {hook} dt={deltaSeconds:F3}");
        return false;
    }

    public override void Shutdown()
    {
        Log.Info($"[game {BuildInfo.Variant}] Shutdown after {tickCount} ticks");
    }
}
