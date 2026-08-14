using System.Runtime.InteropServices;

namespace GkNext.Interop;

// Hand-written counterparts to the cross-boundary structs in src/Modules/NextDotNet/Interop.h.
// Layout must match field for field: these are passed by pointer with no marshalling, so a
// mismatch is silent memory corruption rather than a compile error.
//
// Only the wire layout is fixed. Everything ergonomic — constructors, defaults, named colors —
// is layered on top and never changes the field list.

[StructLayout(LayoutKind.Sequential)]
public struct Vector2(float x, float y)
{
    public float X = x;
    public float Y = y;

    public override readonly string ToString() => $"({X:F3}, {Y:F3})";
}

[StructLayout(LayoutKind.Sequential)]
public struct Vector3(float x, float y, float z)
{
    public float X = x;
    public float Y = y;
    public float Z = z;

    public static Vector3 Zero => new(0.0f, 0.0f, 0.0f);
    public static Vector3 One => new(1.0f, 1.0f, 1.0f);
    public static Vector3 Up => new(0.0f, 1.0f, 0.0f);

    public override readonly string ToString() => $"({X:F3}, {Y:F3}, {Z:F3})";
}

[StructLayout(LayoutKind.Sequential)]
public struct Vector4(float x, float y, float z, float w)
{
    public float X = x;
    public float Y = y;
    public float Z = z;
    public float W = w;

    public override readonly string ToString() => $"({X:F3}, {Y:F3}, {Z:F3}, {W:F3})";
}

/// <summary>
/// A color with 0..1 float components, matching how script authors saw colors under QuickJS.
/// </summary>
/// <remarks>
/// This type never crosses the boundary. The generated wrapper calls <see cref="ToPacked"/> at the
/// call site and passes a single <c>GkColor32</c>, which the native side hands straight to ImGui
/// (design 4.4 decision 1).
/// </remarks>
public readonly struct Color(float r, float g, float b, float a = 1.0f)
{
    public readonly float R = r;
    public readonly float G = g;
    public readonly float B = b;
    public readonly float A = a;

    public static Color White => new(1.0f, 1.0f, 1.0f);
    public static Color Black => new(0.0f, 0.0f, 0.0f);
    public static Color Transparent => new(0.0f, 0.0f, 0.0f, 0.0f);

    public static Color FromBytes(byte r, byte g, byte b, byte a = 255)
        => new(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f);

    /// <summary>Packs into IM_COL32 layout: 0xAABBGGRR.</summary>
    public uint ToPacked()
    {
        static uint ToByte(float value)
        {
            float clamped = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
            return (uint)(clamped * 255.0f + 0.5f);
        }

        return ToByte(R) | (ToByte(G) << 8) | (ToByte(B) << 16) | (ToByte(A) << 24);
    }
}

/// <summary>
/// Everything needed to create a render node except its name, which travels as its own parameter
/// because cross-boundary structs hold no strings.
/// </summary>
/// <remarks>
/// A zero-initialised struct would mean scale 0 and invisible, so the constructor supplies the
/// same defaults the QuickJS spec object had: identity scale and visible.
/// </remarks>
[StructLayout(LayoutKind.Sequential)]
public struct RenderNodeSpec(uint modelId, uint materialId)
{
    public uint ModelId = modelId;
    public uint MaterialId = materialId;
    public Vector3 Translation = Vector3.Zero;
    public Vector3 Scale = Vector3.One;
    public int Visible = 1;

    public readonly bool IsVisible => Visible != 0;

    public RenderNodeSpec WithTranslation(Vector3 translation)
    {
        Translation = translation;
        return this;
    }

    public RenderNodeSpec WithScale(Vector3 scale)
    {
        Scale = scale;
        return this;
    }

    public RenderNodeSpec WithVisible(bool visible)
    {
        Visible = visible ? 1 : 0;
        return this;
    }
}

[StructLayout(LayoutKind.Sequential)]
public struct CameraOverride(Vector3 position, Vector3 target, float fieldOfView)
{
    public Vector3 Position = position;
    public Vector3 Target = target;
    public Vector3 Up = Vector3.Up;
    public float FieldOfView = fieldOfView;
}

/// <summary>
/// Well-known node id values. Mirrors GK_INVALID_NODE_ID in src/Modules/NextDotNet/Interop.h.
/// </summary>
/// <remarks>
/// Node ids start at 0, so 0 is a real node — the first one built into an empty scene, in fact.
/// Code that needs "no node yet" must use <see cref="Invalid"/>; using 0 makes exactly that first
/// node invisible to whatever holds the id.
/// </remarks>
public static class NodeIds
{
    public const uint Invalid = 0xFFFFFFFFu;

    public static bool IsValid(uint nodeId) => nodeId != Invalid;
}

/// <summary>SDL key codes used by gameplay. Raw values, matching what the ABI carries.</summary>
public static class KeyCodes
{
    public const int Escape = 0x1B;
    public const int Space = 0x20;
    public const int Return = 0x0D;
}

public enum InputEventType : int
{
    KeyDown = 0,
    KeyUp = 1,
    MouseButtonDown = 2,
    MouseButtonUp = 3,
    GamepadButtonDown = 4,
    GamepadButtonUp = 5,
}

/// <summary>
/// One input event. Key and gamepad identities are the raw SDL codes: mapping them to names would
/// cost a string allocation per event on a path that runs every frame.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public struct InputEvent
{
    public InputEventType Type;
    public int KeyCode;
    public int MouseButton;
    public int GamepadButton;
    public int Repeated;

    public readonly bool IsRepeat => Repeated != 0;
}

/// <summary>
/// Lifecycle hooks the host raises. Mirrors EScriptHook in src/Modules/NextDotNet/Interop.h.
/// </summary>
public enum ScriptHook : int
{
    OnInit = 0,
    OnDestroy = 1,
    BeforeSceneRebuild = 2,
    OnSceneLoaded = 3,
    OnRenderUI = 4,
}

/// <summary>
/// Managed entry points handed back to the host at bootstrap. Layout must match FManagedApi in
/// src/Modules/NextDotNet/Interop.h.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct FManagedApi
{
    public uint Version;
    public delegate* unmanaged<GkStr, int> LoadGame;
    public delegate* unmanaged<int> UnloadGame;
    public delegate* unmanaged<GkStr, int> ReloadGame;
    public delegate* unmanaged<double, void> Tick;
    public delegate* unmanaged<int, double, int> Lifecycle;
    public delegate* unmanaged<InputEvent*, int> InputEvent;
    public delegate* unmanaged<CameraOverride*, int> OverrideCamera;
}
