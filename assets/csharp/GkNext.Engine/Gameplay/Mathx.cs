using GkNext.Interop;

namespace GkNext;

/// <summary>
/// The scalar helpers gameplay reaches for that <c>MathF</c> does not carry.
/// </summary>
/// <remarks>
/// Small on purpose. This is not a vector maths library — the interop structs are wire layout and
/// stay that way — it is the handful of operations that were otherwise written out by hand in
/// every game, each time with its own chance of an inverted sign.
/// </remarks>
public static class Mathx
{
    public static float Clamp(float value, float min, float max)
        => value < min ? min : (value > max ? max : value);

    /// <summary>Clamps to [0, 1]. The usual last step before a value becomes a bar or an alpha.</summary>
    public static float Saturate(float value) => Clamp(value, 0.0f, 1.0f);

    public static float Lerp(float from, float to, float t) => from + (to - from) * t;

    /// <summary>Steps towards a target without overshooting it.</summary>
    public static float MoveTowards(float current, float target, float maxDelta)
    {
        float difference = target - current;
        return MathF.Abs(difference) <= maxDelta ? target : current + MathF.Sign(difference) * maxDelta;
    }

    /// <summary>Folds an angle into (-π, π].</summary>
    public static float WrapAngle(float radians)
    {
        float wrapped = MathF.IEEERemainder(radians, MathF.Tau);
        return wrapped <= -MathF.PI ? wrapped + MathF.Tau : wrapped;
    }

    /// <summary>
    /// Turns towards a target angle the short way round.
    /// </summary>
    /// <remarks>
    /// The reason this exists rather than <see cref="MoveTowards"/>: angles wrap, so a character
    /// at 3.1 radians turning to -3.1 is 0.08 radians away, not 6.2. Without the wrap it spins
    /// almost the whole way round, which is the single most recognisable bug in a character
    /// controller.
    /// </remarks>
    public static float TurnTowards(float current, float target, float maxDelta)
    {
        float difference = WrapAngle(target - current);
        return MathF.Abs(difference) <= maxDelta ? target : current + MathF.Sign(difference) * maxDelta;
    }

    /// <summary>Length of a vector on the ground plane, where most gameplay distance checks live.</summary>
    public static float LengthXZ(float x, float z) => MathF.Sqrt(x * x + z * z);

    /// <summary>
    /// True when two circles on the ground plane overlap.
    /// </summary>
    /// <remarks>Squared on both sides: a contact test runs per enemy per frame, and the square
    /// root it avoids is the only expensive part of it.</remarks>
    public static bool OverlapsXZ(float ax, float az, float bx, float bz, float radiusSum)
    {
        float dx = ax - bx;
        float dz = az - bz;
        return dx * dx + dz * dz <= radiusSum * radiusSum;
    }
}

/// <summary>
/// Quaternions, for the two cases gameplay actually hits.
/// </summary>
/// <remarks>
/// <see cref="Vector4"/> is the rotation the scene takes, in (x, y, z, w) order. Building one by
/// hand at the call site is how a template used to teach this, and the result was the same eight
/// lines of half-angle trigonometry copied into every game that wanted to spin a box.
/// </remarks>
public static class Quat
{
    public static Vector4 Identity => new(0.0f, 0.0f, 0.0f, 1.0f);

    /// <summary>Yaw: the rotation almost every character and prop on a ground plane needs.</summary>
    public static Vector4 AroundY(float radians)
    {
        float half = radians * 0.5f;
        return new Vector4(0.0f, MathF.Sin(half), 0.0f, MathF.Cos(half));
    }

    /// <summary>Rotation of <paramref name="radians"/> about an arbitrary axis, which need not be unit length.</summary>
    public static Vector4 AroundAxis(in Vector3 axis, float radians)
    {
        float length = MathF.Sqrt(axis.X * axis.X + axis.Y * axis.Y + axis.Z * axis.Z);
        if (length < 1e-6f)
        {
            return Identity;
        }

        float half = radians * 0.5f;
        float sin = MathF.Sin(half) / length;
        return new Vector4(axis.X * sin, axis.Y * sin, axis.Z * sin, MathF.Cos(half));
    }

    /// <summary>
    /// Turns +Z onto <paramref name="direction"/>. What a beam, a tracer or a stretched box needs
    /// to point at something.
    /// </summary>
    public static Vector4 LookAlong(in Vector3 direction)
    {
        float length = MathF.Sqrt(direction.X * direction.X + direction.Y * direction.Y +
                                  direction.Z * direction.Z);
        if (length < 1e-6f)
        {
            return Identity;
        }

        float x = direction.X / length;
        float y = direction.Y / length;
        float z = direction.Z / length;

        // dot(+Z, direction) is just z. The two poles have no unique axis, so they are named.
        if (z > 0.99999f)
        {
            return Identity;
        }
        if (z < -0.99999f)
        {
            return new Vector4(0.0f, 1.0f, 0.0f, 0.0f);   // 180 degrees about Y
        }

        // axis = cross(+Z, direction), angle = acos(z).
        return AroundAxis(new Vector3(-y, x, 0.0f), MathF.Acos(Mathx.Clamp(z, -1.0f, 1.0f)));
    }
}
