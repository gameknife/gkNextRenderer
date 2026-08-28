using GkNext;
using GkNext.Interop;

namespace {{Namespace}};

/// <summary>
/// The RNG a run is reproducible with.
/// </summary>
/// <remarks>
/// Deliberately not <c>System.Random</c>: the same seed has to produce the same run on every
/// platform and every .NET version, which is what makes a recorded run replayable and a bug
/// reproducible from a seed alone.
/// </remarks>
internal sealed class XorShift32
{
    private uint state = 1u;

    public void Reset(uint seed) => state = seed == 0u ? 1u : seed;

    public uint Next()
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }

    /// <summary>Uniform in [min, max).</summary>
    public float NextFloat(float min, float max) => min + (Next() >> 8) * (1.0f / 16777216.0f) * (max - min);
}

/// <summary>
/// The scrolling walls, as a fixed pool.
/// </summary>
/// <remarks>
/// A plain C# class holding node ids — not an engine type, and not something that can be "attached"
/// to a node. That is the shape gameplay code takes here: the engine owns the nodes, this owns what
/// they mean.
///
/// The pool is allocated once and recycled. Nodes are never created or destroyed during play: a
/// wall that leaves the screen is moved back to the right with a fresh gap, which keeps the scene
/// graph stable and the frame free of allocation.
/// </remarks>
internal sealed class ObstacleField(int count, float spacing, float gapHeight, float wallWidth, float worldHalfHeight)
{
    /// <summary>Half the height of one wall box. Both walls are this tall whatever the gap is, so
    /// only their position changes at runtime and their scale is baked into the spec.</summary>
    public const float WallHalfHeight = 8.0f;

    private readonly uint[] topNodes = new uint[count];
    private readonly uint[] bottomNodes = new uint[count];
    private readonly float[] positionX = new float[count];
    private readonly float[] gapCenterY = new float[count];
    private readonly bool[] scored = new bool[count];

    public int Count => count;
    public float WallWidth => wallWidth;
    public float GapHeight => gapHeight;

    public void SetNodes(int index, uint topNode, uint bottomNode)
    {
        topNodes[index] = topNode;
        bottomNodes[index] = bottomNode;
    }

    /// <summary>Parks every wall off to the right in its starting formation.</summary>
    public void Reset(float firstX, XorShift32 rng)
    {
        for (int i = 0; i < count; i++)
        {
            positionX[i] = firstX + i * spacing;
            gapCenterY[i] = NextGapCenter(rng);
            scored[i] = false;
        }
    }

    /// <summary>Scrolls the field left and returns how many walls the player just cleared.</summary>
    public int Advance(float deltaSeconds, float speed, float playerX, float recycleX, XorShift32 rng)
    {
        int scoredThisStep = 0;
        for (int i = 0; i < count; i++)
        {
            positionX[i] -= speed * deltaSeconds;

            if (!scored[i] && positionX[i] + wallWidth * 0.5f < playerX)
            {
                scored[i] = true;
                scoredThisStep++;
            }

            if (positionX[i] < recycleX)
            {
                // Back to the right of the rightmost wall, so spacing survives recycling.
                positionX[i] = Rightmost() + spacing;
                gapCenterY[i] = NextGapCenter(rng);
                scored[i] = false;
            }
        }
        return scoredThisStep;
    }

    /// <summary>True when an axis-aligned box centred on the player overlaps any wall.</summary>
    public bool Collides(float playerX, float playerY, float playerHalfSize)
    {
        for (int i = 0; i < count; i++)
        {
            if (MathF.Abs(positionX[i] - playerX) > wallWidth * 0.5f + playerHalfSize)
            {
                continue;
            }

            float gapTop = gapCenterY[i] + gapHeight * 0.5f;
            float gapBottom = gapCenterY[i] - gapHeight * 0.5f;
            if (playerY + playerHalfSize > gapTop || playerY - playerHalfSize < gapBottom)
            {
                return true;
            }
        }
        return false;
    }

    /// <summary>Pushes the simulated positions onto the scene nodes. Call once per frame, not per
    /// fixed step: the renderer only needs the state the frame ends in.</summary>
    public void SyncNodes()
    {
        for (int i = 0; i < count; i++)
        {
            float top = gapCenterY[i] + gapHeight * 0.5f + WallHalfHeight;
            float bottom = gapCenterY[i] - gapHeight * 0.5f - WallHalfHeight;

            Vector3 topPosition = new(positionX[i], top, 0.0f);
            Vector3 bottomPosition = new(positionX[i], bottom, 0.0f);
            Scene.SetNodeTranslation(topNodes[i], in topPosition);
            Scene.SetNodeTranslation(bottomNodes[i], in bottomPosition);
        }
    }

    private float Rightmost()
    {
        float rightmost = positionX[0];
        for (int i = 1; i < count; i++)
        {
            if (positionX[i] > rightmost)
            {
                rightmost = positionX[i];
            }
        }
        return rightmost;
    }

    private float NextGapCenter(XorShift32 rng)
    {
        float limit = worldHalfHeight - gapHeight * 0.5f - 0.6f;
        return rng.NextFloat(-limit, limit);
    }
}
