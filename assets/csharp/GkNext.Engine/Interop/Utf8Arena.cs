using System.Runtime.InteropServices;
using System.Text;

namespace GkNext.Interop;

/// <summary>
/// Per-thread bump allocator backing <see cref="GkStr"/> arguments.
/// </summary>
/// <remarks>
/// Every string that crosses into native code needs UTF-8 bytes at a stable address for the
/// duration of the call. Allocating a managed array per call would put the gameplay hot path on
/// the GC (design section 9), so encoded bytes come from unmanaged memory that is bump-allocated
/// and released by mark/rewind:
/// <code>
/// var mark = Utf8Arena.Mark();
/// try { Api.UiDrawText(Utf8Arena.Encode(text), x, y); }
/// finally { Utf8Arena.Release(mark); }
/// </code>
/// Storage is a chain of unmanaged blocks that are retired but never freed, so a pointer handed
/// out earlier in the same call stays valid when a later argument forces the arena to grow.
/// Nesting is safe because <see cref="Release"/> only rewinds the bump position. Steady state is
/// zero allocation on both the managed and the native heap.
/// </remarks>
public static unsafe class Utf8Arena
{
    private const int InitialBlockBytes = 4096;

    /// <summary>Opaque bump position. Obtain with <see cref="Mark"/>, restore with <see cref="Release"/>.</summary>
    public readonly struct Position
    {
        internal readonly int BlockIndex;
        internal readonly int Offset;

        internal Position(int blockIndex, int offset)
        {
            BlockIndex = blockIndex;
            Offset = offset;
        }
    }

    private struct Block
    {
        public byte* Data;
        public int Capacity;
    }

    [ThreadStatic] private static Block[]? blocks;
    [ThreadStatic] private static int blockCount;
    [ThreadStatic] private static int currentBlock;
    [ThreadStatic] private static int currentOffset;

    /// <summary>Captures the current bump position. Pair with <see cref="Release"/> in a finally.</summary>
    public static Position Mark() => new(currentBlock, currentOffset);

    /// <summary>Rewinds to a position previously returned by <see cref="Mark"/>.</summary>
    public static void Release(Position position)
    {
        currentBlock = position.BlockIndex;
        currentOffset = position.Offset;
    }

    /// <summary>Encodes <paramref name="value"/> into arena memory valid until the matching Release.</summary>
    public static GkStr Encode(string? value)
    {
        if (string.IsNullOrEmpty(value))
        {
            return GkStr.Empty;
        }

        int maxBytes = Encoding.UTF8.GetMaxByteCount(value.Length);
        byte* destination = Allocate(maxBytes, out int available);
        int written = Encoding.UTF8.GetBytes(value, new Span<byte>(destination, available));
        currentOffset += written;
        return new GkStr(destination, written);
    }

    private static byte* Allocate(int bytes, out int available)
    {
        Block[] table = blocks ??= new Block[8];

        if (blockCount > 0)
        {
            ref Block active = ref table[currentBlock];
            if (active.Capacity - currentOffset >= bytes)
            {
                available = active.Capacity - currentOffset;
                return active.Data + currentOffset;
            }
        }

        // The active block is full. Retired blocks are kept alive so that pointers handed out for
        // earlier arguments of the same call remain valid; advance into the next one, reusing it
        // when this thread has already grown that far before.
        for (int index = currentBlock + 1; index < blockCount; index++)
        {
            if (table[index].Capacity >= bytes)
            {
                currentBlock = index;
                currentOffset = 0;
                available = table[index].Capacity;
                return table[index].Data;
            }
        }

        int capacity = InitialBlockBytes;
        if (blockCount > 0)
        {
            capacity = Math.Max(capacity, table[blockCount - 1].Capacity * 2);
        }
        capacity = Math.Max(capacity, bytes);

        if (blockCount == table.Length)
        {
            Array.Resize(ref table, table.Length * 2);
            blocks = table;
        }

        table[blockCount] = new Block
        {
            Data = (byte*)NativeMemory.Alloc((nuint)capacity),
            Capacity = capacity,
        };
        currentBlock = blockCount;
        currentOffset = 0;
        blockCount++;

        available = capacity;
        return table[currentBlock].Data;
    }
}
