namespace Flappy;

/// <summary>
/// The same xorshift32 the C++ side uses. Reproduced operation for operation: the pipe layout, and
/// therefore the whole replay, is decided by this sequence.
/// </summary>
public sealed class XorShift32(uint seed = 0x00C0FFEEu)
{
    private uint state = seed == 0 ? 0x00C0FFEEu : seed;

    public uint NextU32()
    {
        uint x = state;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        state = x;
        return x;
    }

    public float NextFloat01()
    {
        const float invMax = 1.0f / 4294967295.0f;
        return NextU32() * invMax;
    }

    public uint State => state;

    public void Reset(uint seed) => state = seed == 0 ? 0x00C0FFEEu : seed;
}
