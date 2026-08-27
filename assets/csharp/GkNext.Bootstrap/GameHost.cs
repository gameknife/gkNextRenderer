#if !GK_AOT
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.Loader;
#endif

using GkNext.Interop;

namespace GkNext.Bootstrap;

/// <summary>
/// Owns the game module's lifetime. This is the only file in the managed tree allowed to observe
/// the backend (design section 3.3); game and framework code must contain no <c>#if</c>.
/// </summary>
/// <remarks>
/// The native host calls Load / Unload / Reload without knowing which backend is active. Under
/// CoreCLR a reload swaps a collectible AssemblyLoadContext; under NativeAOT the game is linked in
/// and a reload is reported as unavailable. The function pointers the host holds never change
/// either way, so the host stays unaware of how a reload is implemented.
/// </remarks>
internal static class GameHost
{
    /// <summary>
    /// The type GkNext.SourceGen emits into every game assembly from its [GameInstance] attribute.
    /// Fixed by contract: the CoreCLR host looks it up by name, the AOT host calls it directly.
    /// </summary>
    private const string GeneratedEntryTypeName = "GkNext.Generated.GameEntry";

    /// <summary>Load succeeded.</summary>
    public const int StatusOk = 0;

    /// <summary>Reload is not available on this backend (NativeAOT).</summary>
    public const int StatusReloadUnavailable = 2;

    /// <summary>Unload completed, but the load context had not been collected yet.</summary>
    public const int StatusUnloadPending = 3;

    /// <summary>The requested assembly or its entry type could not be found.</summary>
    public const int StatusNotFound = -10;

    public static IGameModule? Current { get; private set; }

    public static int Load(string assemblyPath)
    {
        if (Current != null)
        {
            Unload();
        }

        int status = CreateModule(assemblyPath, out IGameModule? module);
        if (status != StatusOk || module == null)
        {
            return status;
        }

        Current = module;
        Current.Initialize();
        return StatusOk;
    }

    public static int Reload(string assemblyPath)
    {
#if GK_AOT
        _ = assemblyPath;
        return StatusReloadUnavailable;
#else
        int unloadStatus = Unload();
        int loadStatus = Load(assemblyPath);
        return loadStatus != StatusOk ? loadStatus : unloadStatus;
#endif
    }

#if GK_AOT

    private static int CreateModule(string assemblyPath, out IGameModule? module)
    {
        // The game is linked into this binary; the path is only meaningful to the CoreCLR host.
        _ = assemblyPath;
        module = Generated.GameEntry.Create();
        return StatusOk;
    }

    public static int Unload()
    {
        Current?.Shutdown();
        Current = null;
        return StatusOk;
    }

#else

    private const int UnloadCollectionAttempts = 10;

    private static GameLoadContext? loadContext;
    private static WeakReference? loadContextTracker;

    /// <summary>
    /// The context GkNext.Bootstrap itself was loaded into. Under the CoreCLR host that is not the
    /// default context: hostfxr loads a component into its own context built from the component's
    /// deps.json, so falling back to the default context would fail to find GkNext.Engine.
    /// </summary>
    private static readonly AssemblyLoadContext HostContext =
        AssemblyLoadContext.GetLoadContext(typeof(GameHost).Assembly) ?? AssemblyLoadContext.Default;

    /// <summary>
    /// A collectible context that resolves every dependency from the host context. Loading
    /// GkNext.Engine into this context instead would give the game a second, incompatible copy of
    /// <see cref="IGameModule"/>, and the cast back across the boundary would fail.
    /// </summary>
    private sealed class GameLoadContext() : AssemblyLoadContext("GkNext.Game", isCollectible: true)
    {
        protected override Assembly? Load(AssemblyName assemblyName)
        {
            try
            {
                return HostContext.LoadFromAssemblyName(assemblyName);
            }
            catch (FileNotFoundException)
            {
                // Let the runtime's own resolution report the failure with full context.
                return null;
            }
        }
    }

    // Reflection is confined to this branch. It is never compiled into the NativeAOT build, where
    // CreateModule above is a direct static call, so no reflection reaches ILC.
#pragma warning disable IL2026, IL2072, IL2075, IL3050
    private static int CreateModule(string assemblyPath, out IGameModule? module)
    {
        module = null;
        if (!File.Exists(assemblyPath))
        {
            return StatusNotFound;
        }

        loadContext = new GameLoadContext();
        loadContextTracker = new WeakReference(loadContext, trackResurrection: true);

        // Load from bytes so the file on disk stays writable: a rebuild during a running session
        // is exactly the case hot reload exists for.
        byte[] assemblyBytes = File.ReadAllBytes(assemblyPath);
        string symbolPath = Path.ChangeExtension(assemblyPath, ".pdb");
        using var assemblyStream = new MemoryStream(assemblyBytes);

        Assembly assembly;
        if (File.Exists(symbolPath))
        {
            using var symbolStream = new MemoryStream(File.ReadAllBytes(symbolPath));
            assembly = loadContext.LoadFromStream(assemblyStream, symbolStream);
        }
        else
        {
            assembly = loadContext.LoadFromStream(assemblyStream);
        }

        Type? entryType = assembly.GetType(GeneratedEntryTypeName, throwOnError: false);
        MethodInfo? factory = entryType?.GetMethod("Create", BindingFlags.Public | BindingFlags.Static);
        if (factory == null)
        {
            return StatusNotFound;
        }

        module = factory.Invoke(null, null) as IGameModule;
        return module == null ? StatusNotFound : StatusOk;
    }
#pragma warning restore IL2026, IL2072, IL2075, IL3050

    public static int Unload()
    {
        if (!ShutdownAndUnload())
        {
            return StatusOk;
        }

        // Collection is not synchronous. Draining it here means a reload that leaks a reference
        // shows up immediately as StatusUnloadPending instead of as slow memory growth.
        for (int attempt = 0; attempt < UnloadCollectionAttempts; attempt++)
        {
            if (loadContextTracker?.IsAlive != true)
            {
                loadContextTracker = null;
                return StatusOk;
            }

            GC.Collect();
            GC.WaitForPendingFinalizers();
        }

        return loadContextTracker?.IsAlive == true ? StatusUnloadPending : StatusOk;
    }

    /// <summary>
    /// Drops every reference to the game assembly and unloads its context. Returns false when
    /// there was nothing loaded.
    /// </summary>
    /// <remarks>
    /// Deliberately a separate, non-inlined frame from the collection loop in <see cref="Unload"/>.
    /// Tier-0 JIT reports every local in a frame as a GC root for the whole method, so running the
    /// collection loop in the same frame that touched the game module would keep the module — and
    /// therefore its load context — alive and report a false leak.
    /// </remarks>
    [MethodImpl(MethodImplOptions.NoInlining)]
    private static bool ShutdownAndUnload()
    {
        Current?.Shutdown();
        Current = null;

        if (loadContext == null)
        {
            return false;
        }

        loadContext.Unload();
        loadContext = null;
        return true;
    }

#endif
}
