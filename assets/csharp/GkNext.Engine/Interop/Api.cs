namespace GkNext.Interop;

/// <summary>
/// Holds the engine function table for the lifetime of the process.
/// </summary>
/// <remarks>
/// Bound once by GkNext.Bootstrap and read by every generated wrapper in Engine.g.cs. It lives in
/// the default AssemblyLoadContext, so a hot reload of the game assembly does not disturb it.
/// </remarks>
public static unsafe class Api
{
    /// <summary>ABI version negotiated at bootstrap. Must match GK_DOTNET_ABI_VERSION.</summary>
    public const uint AbiVersion = 3;

    public static FEngineApi* Table { get; private set; }

    public static bool IsReady => Table != null;

    /// <summary>
    /// Called by the bootstrap assembly at startup. Game code must never call this: rebinding the
    /// table mid-session would point live code at a different engine instance.
    /// </summary>
    /// <remarks>
    /// Public rather than internal-with-InternalsVisibleTo because each NativeAOT application
    /// publishes the bootstrap under its own assembly name, and InternalsVisibleTo cannot name
    /// them all.
    /// </remarks>
    public static void Bind(FEngineApi* table) => Table = table;
}
