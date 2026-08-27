using GkNext.Interop;

namespace Brotato3D;

public enum AppState
{
    MainMenu,
    CharacterSelect,
    Playing,
    Paused,
    LevelUpPicking,
    Shopping,
    Result,
}

public struct PlayerRuntime
{
    public Vector3 Position;
    public Vector3 Facing;
    public PlayerStats Stats;
    public int CurrentHp;
    public int MaxHp;
    public int CurrentXp;
    public int Level;
    public int Materials;
    public int PendingLevelUps;
    public int DashCharges;
    public float DashCooldownMs;
    public float DashRemainingMs;
    public Vector3 DashDirection;
}

public struct EnemyRuntime
{
    public EnemyDef? Def;
    public uint NodeId;
    public uint PushBodyId;
    public Vector3 Position;
    public int CurrentHp;
    public float Radius;
    public float ContactCooldownMs;
    public float RangedCooldownMs;
    public float HitFlashMs;
    public bool Active;
    public bool PushBodyActive;
}

public struct ProjectileRuntime
{
    public WeaponDef? Weapon;
    public uint NodeId;
    public Vector3 Position;
    public Vector3 Velocity;
    public float LifetimeMs;
    public float Radius;
    public int Damage;
    public int PierceRemaining;
    public int LastHitEnemySlot;
    public bool EnemyOwned;
    public bool Active;
}

public struct PickupRuntime
{
    public uint NodeId;
    public Vector3 Position;
    public int Xp;
    public int Materials;
    public bool Active;
}

public enum DebrisKind
{
    Tiny,
    Chunk,
    BossChunk,
}

public struct DebrisRuntime
{
    public uint NodeId;
    public uint BodyId;
    public float LifetimeMs;
    public bool Active;
}

public struct SpawnRuntime
{
    public SpawnDef? Def;
    public int Remaining;
    public float TimerMs;
}

/// <summary>Small deterministic RNG used by gameplay; no System.Random allocation or virtual calls.</summary>
public struct BrotatoRng(uint seed)
{
    private uint state = seed == 0 ? 0xB07A703Du : seed;

    public uint NextU32()
    {
        uint value = state;
        value ^= value << 13;
        value ^= value >> 17;
        value ^= value << 5;
        state = value;
        return value;
    }

    public float NextFloat() => NextU32() * (1.0f / uint.MaxValue);

    public int NextInt(int maxExclusive)
        => maxExclusive <= 1 ? 0 : (int)(NextU32() % (uint)maxExclusive);

    public float Range(float min, float max) => min + (max - min) * NextFloat();

    public void Reset(uint seed) => state = seed == 0 ? 0xB07A703Du : seed;
}

public static class BrotatoMath
{
    public static Vector3 Add(Vector3 a, Vector3 b) => new(a.X + b.X, a.Y + b.Y, a.Z + b.Z);
    public static Vector3 Subtract(Vector3 a, Vector3 b) => new(a.X - b.X, a.Y - b.Y, a.Z - b.Z);
    public static Vector3 Multiply(Vector3 value, float scalar) => new(value.X * scalar, value.Y * scalar, value.Z * scalar);

    public static float LengthXZ(Vector3 value) => MathF.Sqrt(value.X * value.X + value.Z * value.Z);

    public static float DistanceXZ(Vector3 a, Vector3 b) => LengthXZ(Subtract(a, b));

    public static Vector3 NormalizeXZ(Vector3 value)
    {
        float length = LengthXZ(value);
        return length > 0.0001f ? new Vector3(value.X / length, 0.0f, value.Z / length) : Vector3.Zero;
    }

    public static Vector3 RotateY(Vector3 value, float radians)
    {
        float cosine = MathF.Cos(radians);
        float sine = MathF.Sin(radians);
        return NormalizeXZ(new Vector3(value.X * cosine - value.Z * sine,
                                       0.0f,
                                       value.X * sine + value.Z * cosine));
    }

    public static Vector3 ClampToArena(Vector3 position, float radius, Vector2 halfExtent)
        => new(Math.Clamp(position.X, -halfExtent.X + radius, halfExtent.X - radius),
               position.Y,
               Math.Clamp(position.Z, -halfExtent.Y + radius, halfExtent.Y - radius));

    public static Vector4 RotationFromDirection(Vector3 direction)
    {
        if (LengthXZ(direction) <= 0.0001f)
        {
            return new Vector4(0.0f, 0.0f, 0.0f, 1.0f);
        }
        float yaw = MathF.Atan2(direction.X, -direction.Z);
        float half = yaw * 0.5f;
        return new Vector4(0.0f, MathF.Sin(half), 0.0f, MathF.Cos(half));
    }
}
