using System.Runtime.InteropServices;

namespace GkNext.Interop;

/// <summary>
/// The one string representation that crosses the native boundary: a UTF-8 byte range that the
/// caller owns for the duration of the call. Blittable by construction, so it needs no marshalling
/// stub under either backend.
/// </summary>
/// <remarks>
/// Mirrors <c>GkStr</c> in src/Modules/NextDotNet/Interop.h. The bytes are not required to be
/// null-terminated; native code must always honour <see cref="Length"/>.
/// </remarks>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct GkStr
{
    public byte* Data;
    public int Length;

    public GkStr(byte* data, int length)
    {
        Data = data;
        Length = length;
    }

    public static GkStr Empty => new(null, 0);

    /// <summary>Copies the range into a managed string. Allocates; not for per-frame paths.</summary>
    public override readonly string ToString()
        => Data == null || Length <= 0 ? string.Empty : System.Text.Encoding.UTF8.GetString(Data, Length);
}
