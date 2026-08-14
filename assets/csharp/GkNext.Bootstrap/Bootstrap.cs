using System.Runtime.InteropServices;
using GkNext.Interop;

namespace GkNext.Bootstrap;

/// <summary>
/// The single native entry point. Everything else crosses the boundary through the two function
/// tables this method exchanges.
/// </summary>
/// <remarks>
/// Identical under both backends: NativeAOT exports <c>GkNext_Bootstrap</c> from the produced
/// library, CoreCLR resolves the same method through
/// <c>load_assembly_and_get_function_pointer</c> with UNMANAGEDCALLERSONLY_METHOD. That symmetry
/// is what lets both hosts share one managed codebase.
/// </remarks>
public static unsafe class Bootstrap
{
    [UnmanagedCallersOnly(EntryPoint = "GkNext_Bootstrap")]
    public static int Initialize(FEngineApi* engineApi, FManagedApi* outManagedApi)
    {
        if (engineApi == null || outManagedApi == null)
        {
            return -1;
        }

        if (engineApi->Version != Api.AbiVersion)
        {
            return -2;
        }

        Api.Bind(engineApi);

        outManagedApi->Version = Api.AbiVersion;
        outManagedApi->LoadGame = &LoadGame;
        outManagedApi->UnloadGame = &UnloadGame;
        outManagedApi->ReloadGame = &ReloadGame;
        outManagedApi->Tick = &Tick;
        outManagedApi->Lifecycle = &Lifecycle;
        outManagedApi->InputEvent = &OnInputEvent;
        outManagedApi->OverrideCamera = &OverrideCamera;

        return 0;
    }

    [UnmanagedCallersOnly]
    private static int LoadGame(GkStr assemblyPath) => Guard(() => GameHost.Load(assemblyPath.ToString()));

    [UnmanagedCallersOnly]
    private static int UnloadGame() => Guard(GameHost.Unload);

    [UnmanagedCallersOnly]
    private static int ReloadGame(GkStr assemblyPath) => Guard(() => GameHost.Reload(assemblyPath.ToString()));

    [UnmanagedCallersOnly]
    private static void Tick(double deltaSeconds)
    {
        try
        {
            GameHost.Current?.Tick(deltaSeconds);
        }
        catch (Exception exception)
        {
            Report(exception);
        }
    }

    [UnmanagedCallersOnly]
    private static int Lifecycle(int hook, double deltaSeconds)
    {
        try
        {
            return GameHost.Current?.Lifecycle((ScriptHook)hook, deltaSeconds) == true ? 1 : 0;
        }
        catch (Exception exception)
        {
            Report(exception);
            return 0;
        }
    }

    [UnmanagedCallersOnly]
    private static int OnInputEvent(InputEvent* inputEvent)
    {
        try
        {
            if (inputEvent == null || GameHost.Current == null)
            {
                return 0;
            }
            return GameHost.Current.OnInputEvent(in *inputEvent) ? 1 : 0;
        }
        catch (Exception exception)
        {
            Report(exception);
            return 0;
        }
    }

    [UnmanagedCallersOnly]
    private static int OverrideCamera(CameraOverride* camera)
    {
        try
        {
            if (camera == null || GameHost.Current == null)
            {
                return 0;
            }
            return GameHost.Current.OverrideCamera(ref *camera) ? 1 : 0;
        }
        catch (Exception exception)
        {
            Report(exception);
            return 0;
        }
    }

    /// <summary>
    /// Managed exceptions must never unwind into native frames — the behaviour differs between the
    /// two backends and there is no safe recovery on the native side. Everything is caught here,
    /// reported through the engine log, and surfaced as a negative status code.
    /// </summary>
    private static int Guard(Func<int> action)
    {
        try
        {
            return action();
        }
        catch (Exception exception)
        {
            Report(exception);
            return -100;
        }
    }

    private static void Report(Exception exception)
    {
        if (Api.IsReady)
        {
            Log.Error(exception.ToString());
        }
    }
}
