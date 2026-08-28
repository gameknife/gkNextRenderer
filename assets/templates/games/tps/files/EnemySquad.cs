using GkNext;
using GkNext.Interop;

namespace {{Namespace}};

/// <summary>
/// The infected, as a fixed pool of ScadRig instances.
/// </summary>
/// <remarks>
/// A plain C# class holding rig handles. The rig pool itself was declared during the scene build —
/// that is when the models could still be injected — so spawning here is only ever taking a slot
/// that already exists. Acquire can therefore fail, and a spawn director has to cope rather than
/// assume.
///
/// There is no pathfinding: they walk at the player in a straight line. Cover blocks the shot, not
/// the walk, which is the honest shape of a template — <c>NextGameplay</c>'s FCharacterPool is
/// where nav grids and path following live when a game needs them.
/// </remarks>
internal sealed class EnemySquad(int capacity)
{
    public const float Radius = 0.45f;
    private const float CorpseSeconds = 2.2f;
    private const float HitStunSeconds = 0.35f;

    private readonly uint[] rigIds = new uint[capacity];
    private readonly float[] positionX = new float[capacity];
    private readonly float[] positionZ = new float[capacity];
    private readonly float[] health = new float[capacity];
    private readonly float[] stateTimer = new float[capacity];
    private readonly EnemyState[] states = new EnemyState[capacity];
    /// Whether the walk clip was swapped for the attack one. Tracked rather than re-played every
    /// frame: PlayClip on the current clip is a no-op, but it still crosses the boundary.
    private readonly bool[] attacking = new bool[capacity];

    private enum EnemyState
    {
        Free,
        Chasing,
        Stunned,
        Dying,
    }

    public int Capacity => capacity;
    public int AliveCount { get; private set; }

    /// <summary>Releases every rig instance. Safe before the scene exists — it holds no nodes.</summary>
    public void Reset()
    {
        for (int i = 0; i < capacity; i++)
        {
            if (states[i] != EnemyState.Free && rigIds[i] != 0)
            {
                Rig.Release(rigIds[i]);
            }
            rigIds[i] = 0;
            states[i] = EnemyState.Free;
        }
        AliveCount = 0;
    }

    /// <summary>Takes a pool slot and drops one infected at (x, z). False when the pool is full,
    /// which is the spawn cap: silently dropping the spawn would hide it.</summary>
    public bool TrySpawn(uint poolId, float x, float z, float startHealth, in Vector3 tint)
    {
        for (int i = 0; i < capacity; i++)
        {
            if (states[i] != EnemyState.Free)
            {
                continue;
            }

            Vector3 position = new(x, 0.0f, z);
            uint rig = Rig.Acquire(poolId, in position, 0.0f, in tint);
            if (rig == 0)
            {
                return false;
            }

            rigIds[i] = rig;
            positionX[i] = x;
            positionZ[i] = z;
            health[i] = startHealth;
            states[i] = EnemyState.Chasing;
            stateTimer[i] = 0.0f;
            attacking[i] = false;
            AliveCount++;
            Rig.PlayClip(rigIds[i], "walk", 0.0f);
            return true;
        }
        return false;
    }

    /// <summary>Walks everyone at the player and reports how many are touching them this frame.</summary>
    public int Advance(float deltaSeconds, float speed, float playerX, float playerZ, float contactDistance)
    {
        int contacts = 0;
        for (int i = 0; i < capacity; i++)
        {
            switch (states[i])
            {
                case EnemyState.Free:
                    continue;

                case EnemyState.Dying:
                    stateTimer[i] -= deltaSeconds;
                    if (stateTimer[i] <= 0.0f)
                    {
                        Rig.Release(rigIds[i]);
                        rigIds[i] = 0;
                        states[i] = EnemyState.Free;
                    }
                    continue;

                case EnemyState.Stunned:
                    stateTimer[i] -= deltaSeconds;
                    if (stateTimer[i] <= 0.0f)
                    {
                        states[i] = EnemyState.Chasing;
                        attacking[i] = false;
                        Rig.PlayClip(rigIds[i], "walk", 0.12f);
                    }
                    break;

                case EnemyState.Chasing:
                    break;
            }

            float toX = playerX - positionX[i];
            float toZ = playerZ - positionZ[i];
            float distance = MathF.Sqrt(toX * toX + toZ * toZ);
            if (distance > 0.0001f && states[i] == EnemyState.Chasing)
            {
                float step = speed * deltaSeconds;
                if (distance > contactDistance)
                {
                    positionX[i] += toX / distance * step;
                    positionZ[i] += toZ / distance * step;
                    if (attacking[i])
                    {
                        attacking[i] = false;
                        Rig.PlayClip(rigIds[i], "walk", 0.12f);
                    }
                }
                else
                {
                    contacts++;
                    if (!attacking[i])
                    {
                        attacking[i] = true;
                        Rig.PlayClip(rigIds[i], "attack", 0.08f);
                    }
                }
            }

            // Facing follows the player even while stunned: yaw is atan2(x, z) because the engine's
            // forward is +Z, not +X.
            float yaw = MathF.Atan2(toX, toZ);
            Vector3 position = new(positionX[i], 0.0f, positionZ[i]);
            Rig.SetTransform(rigIds[i], in position, yaw);
        }
        return contacts;
    }

    /// <summary>The live enemy closest to a ray from <paramref name="fromX"/>/<paramref name="fromZ"/>,
    /// or -1. Angular tolerance rather than a real cast: a template should not carry a physics
    /// query, and this is what "the one you are pointing at" means.</summary>
    public int PickTarget(float fromX, float fromZ, Vector3 direction, float maxRange, float maxAngleCos)
    {
        int best = -1;
        float bestDistance = maxRange;
        for (int i = 0; i < capacity; i++)
        {
            if (states[i] != EnemyState.Chasing && states[i] != EnemyState.Stunned)
            {
                continue;
            }

            float toX = positionX[i] - fromX;
            float toZ = positionZ[i] - fromZ;
            float distance = MathF.Sqrt(toX * toX + toZ * toZ);
            if (distance < 0.0001f || distance > bestDistance)
            {
                continue;
            }

            // Flat dot product: aim height is cosmetic here, and comparing in 3D would make a shot
            // miss because the camera was looking slightly down.
            float dot = (toX / distance) * direction.X + (toZ / distance) * direction.Z;
            float flatLength = MathF.Sqrt(direction.X * direction.X + direction.Z * direction.Z);
            if (flatLength < 0.0001f || dot / flatLength < maxAngleCos)
            {
                continue;
            }

            best = i;
            bestDistance = distance;
        }
        return best;
    }

    public Vector3 PositionOf(int index) => new(positionX[index], 0.9f, positionZ[index]);

    /// <summary>Applies damage. Returns true when this shot killed it, so the caller can score.</summary>
    public bool Damage(int index, float amount)
    {
        if (states[index] != EnemyState.Chasing && states[index] != EnemyState.Stunned)
        {
            return false;
        }

        health[index] -= amount;
        if (health[index] > 0.0f)
        {
            states[index] = EnemyState.Stunned;
            stateTimer[index] = HitStunSeconds;
            attacking[index] = false;
            Rig.PlayClip(rigIds[index], "hit", 0.05f);
            return false;
        }

        states[index] = EnemyState.Dying;
        stateTimer[index] = CorpseSeconds;
        AliveCount--;
        // The corpse is not released yet: the clip has to finish, and the slot is not needed until
        // it does.
        Rig.PlayClip(rigIds[index], "die", 0.05f);
        return true;
    }
}
