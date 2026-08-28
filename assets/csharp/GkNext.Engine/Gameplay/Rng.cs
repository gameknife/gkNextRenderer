using GkNext.Interop;

namespace GkNext;

/// <summary>
/// A deterministic random number generator: the same seed gives the same sequence, everywhere.
/// </summary>
/// <remarks>
/// Gameplay wants <c>System.Random</c> nowhere near it. Its sequence is an implementation detail
/// of the runtime, so a seed that produced a level you liked on one machine produces a different
/// one on the next .NET release — which makes a recorded run unreplayable and a bug report from a
/// player impossible to reproduce. This is xorshift32, thirteen instructions, fixed forever.
///
/// A class rather than a struct on purpose. The generator is mutated on every draw, and a struct
/// held in a <c>readonly</c> field is silently copied before each call — every draw would then
/// return the same number, with nothing to see at the call site. One allocation at construction
/// buys immunity from that; no method here allocates.
///
/// A game that has to match a sequence produced outside C# keeps its own generator instead. That
/// is why <c>Flappy</c> ships <c>XorShift32</c>: it reproduces the C++ implementation operation
/// for operation so the two builds replay each other.
/// </remarks>
public sealed class Rng
{
    /// <summary>Any non-zero value works; xorshift is only degenerate at zero.</summary>
    public const uint DefaultSeed = 0x00C0FFEEu;

    private uint state;

    public Rng(uint seed = DefaultSeed) => state = Sanitize(seed);

    /// <summary>The raw generator state. Log it to make a run reproducible from a bug report.</summary>
    public uint State => state;

    /// <summary>Restarts the sequence. Call this from a run reset so every run is the same run.</summary>
    public void Reset(uint seed = DefaultSeed) => state = Sanitize(seed);

    public uint NextUInt()
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }

    /// <summary>Uniform in [0, 1).</summary>
    /// <remarks>
    /// Built from the top 24 bits, which is every bit a float can hold. Scaling the full 32 bits
    /// instead rounds the largest draws up to exactly 1.0f, and a "fraction" that can equal one is
    /// the kind of thing that indexes one past the end of a table twice a day.
    /// </remarks>
    public float NextFloat() => (NextUInt() >> 8) * (1.0f / 16777216.0f);

    /// <summary>Uniform in [min, max).</summary>
    public float NextFloat(float min, float max) => min + NextFloat() * (max - min);

    /// <summary>Uniform in [minInclusive, maxExclusive). Returns min when the range is empty.</summary>
    public int NextInt(int minInclusive, int maxExclusive)
    {
        int range = maxExclusive - minInclusive;
        return range <= 0 ? minInclusive : minInclusive + (int)(NextUInt() % (uint)range);
    }

    /// <summary>Uniform in [0, 2π). The usual way to pick a direction on a spawn ring.</summary>
    public float NextAngle() => NextFloat() * MathF.Tau;

    /// <summary>True with the given probability, clamped to [0, 1].</summary>
    public bool NextChance(float probability) => NextFloat() < probability;

    /// <summary>A unit vector on the XZ plane, for spawning around a point.</summary>
    public Vector3 NextDirectionXZ()
    {
        float angle = NextAngle();
        return new Vector3(MathF.Cos(angle), 0.0f, MathF.Sin(angle));
    }

    private static uint Sanitize(uint seed) => seed == 0u ? DefaultSeed : seed;
}
