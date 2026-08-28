using GkNext;
using GkNext.Interop;

namespace {{Namespace}};

/// <summary>
/// Every enemy in the arena, as one fixed pool.
/// </summary>
/// <remarks>
/// A plain C# class holding node ids. There is no component to attach and no per-enemy script: the
/// engine owns the nodes, this owns what they mean, and the game class owns when it runs.
///
/// Nodes are created once, at full capacity, and then only moved and shown or hidden. Spawning by
/// building nodes and despawning by destroying them would rebuild acceleration structures during
/// play, which is the difference between a steady frame and a stutter every wave.
/// </remarks>
internal sealed class EnemySwarm(int capacity, float radius)
{
    private readonly uint[] nodes = new uint[capacity];
    private readonly float[] positionX = new float[capacity];
    private readonly float[] positionZ = new float[capacity];
    private readonly bool[] alive = new bool[capacity];
    /// <summary>What each node's visibility was last pushed as, so a frame that changes nothing
    /// makes no interop calls.</summary>
    private readonly bool[] nodeVisible = new bool[capacity];

    public int Capacity => capacity;
    public float Radius => radius;
    public int AliveCount { get; private set; }

    public void SetNode(int index, uint node) => nodes[index] = node;

    /// <summary>Kills everything. Safe to call before the scene is committed — it touches no node.</summary>
    public void Reset()
    {
        Array.Clear(alive);
        AliveCount = 0;
    }

    /// <summary>Takes the first free slot. Returns false when the pool is full, which is the
    /// spawn cap: a director that ignores this would silently drop spawns instead.</summary>
    public bool TrySpawn(float x, float z)
    {
        for (int i = 0; i < capacity; i++)
        {
            if (alive[i])
            {
                continue;
            }
            alive[i] = true;
            positionX[i] = x;
            positionZ[i] = z;
            AliveCount++;
            return true;
        }
        return false;
    }

    /// <summary>Moves every live enemy straight at the player.</summary>
    public void Advance(float deltaSeconds, float speed, float playerX, float playerZ)
    {
        for (int i = 0; i < capacity; i++)
        {
            if (!alive[i])
            {
                continue;
            }

            float toX = playerX - positionX[i];
            float toZ = playerZ - positionZ[i];
            float distance = MathF.Sqrt(toX * toX + toZ * toZ);
            if (distance < 0.0001f)
            {
                continue;
            }

            float step = speed * deltaSeconds;
            positionX[i] += toX / distance * step;
            positionZ[i] += toZ / distance * step;
        }
    }

    /// <summary>Kills every enemy touching the player and reports how many. The caller decides what
    /// a contact costs.</summary>
    public int ConsumeContacts(float playerX, float playerZ, float contactDistance)
    {
        int contacts = 0;
        float contactSquared = contactDistance * contactDistance;
        for (int i = 0; i < capacity; i++)
        {
            if (!alive[i])
            {
                continue;
            }

            float dx = positionX[i] - playerX;
            float dz = positionZ[i] - playerZ;
            if (dx * dx + dz * dz <= contactSquared)
            {
                alive[i] = false;
                AliveCount--;
                contacts++;
            }
        }
        return contacts;
    }

    /// <summary>Pushes the simulation onto the scene. Once per frame, after the last step.</summary>
    public void SyncNodes(float groundY)
    {
        for (int i = 0; i < capacity; i++)
        {
            if (alive[i])
            {
                Vector3 position = new(positionX[i], groundY + radius, positionZ[i]);
                Scene.SetNodeTranslation(nodes[i], in position);
            }

            if (nodeVisible[i] != alive[i])
            {
                nodeVisible[i] = alive[i];
                Scene.SetNodeVisible(nodes[i], alive[i]);
            }
        }
    }
}
