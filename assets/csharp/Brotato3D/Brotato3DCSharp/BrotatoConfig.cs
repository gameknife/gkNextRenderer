using System.Text.Json;
using GkNext;
using GkNext.Interop;

namespace Brotato3D;

public struct PlayerStats
{
    public float MaxHpFlat;
    public float DamagePct;
    public float DamageFlat;
    public float AttackSpeedPct;
    public float RangePct;
    public float MoveSpeedPct;
    public float PickupRadiusPct;
    public float CritChancePct;
    public float CritMultiplier;

    public static PlayerStats Default => new()
    {
        MaxHpFlat = 50.0f,
        CritChancePct = 0.05f,
        CritMultiplier = 2.0f,
    };
}

public sealed class CharacterDef
{
    public required string Id { get; init; }
    public required string Name { get; init; }
    public required string Tagline { get; init; }
    public required string StartWeapon { get; init; }
    public Vector3 Color { get; init; } = Vector3.One;
    public PlayerStats StartStats { get; init; } = PlayerStats.Default;
}

public sealed class WeaponDef
{
    public required string Id { get; init; }
    public required string Name { get; init; }
    public int Damage { get; init; } = 1;
    public float AttackSpeedHz { get; init; } = 1.0f;
    public float RangeMeters { get; init; } = 1.0f;
    public float ProjectileSpeed { get; init; } = 10.0f;
    public float ProjectileLifetimeMs { get; init; } = 500.0f;
    public Vector3 ProjectileColor { get; init; } = Vector3.One;
    public float ProjectileSize { get; init; } = 0.12f;
    public int Pellets { get; init; } = 1;
    public float SpreadDegrees { get; init; }
    public int PierceCount { get; init; }
    public float ExplosionRadius { get; init; }
    public int ExplosionDamage { get; init; }
    public bool InstantHit { get; init; }
    public float CritChanceBonus { get; init; }
    public float KnockbackMeters { get; init; }
}

public sealed class EnemyRangedDef
{
    public bool Enabled { get; init; }
    public int Damage { get; init; }
    public float Speed { get; init; }
    public float LifetimeMs { get; init; }
    public Vector3 Color { get; init; } = new(0.3f, 0.95f, 0.2f);
    public float Size { get; init; } = 0.18f;
    public float IntervalMs { get; init; }
    public float PreferredDistance { get; init; }
}

public sealed class EnemyDef
{
    public required string Id { get; init; }
    public required string Name { get; init; }
    public int Hp { get; init; } = 1;
    public float MoveSpeed { get; init; } = 1.0f;
    public int ContactDamage { get; init; } = 1;
    public Vector3 Size { get; init; } = new(0.5f, 0.5f, 0.5f);
    public Vector3 Color { get; init; } = Vector3.One;
    public int XpDrop { get; init; } = 1;
    public int MaterialDrop { get; init; } = 1;
    public float KitingDistance { get; init; }
    public bool IsBoss { get; init; }
    public EnemyRangedDef Ranged { get; init; } = new();
}

public sealed class SpawnDef
{
    public required string EnemyId { get; init; }
    public int Count { get; init; }
    public float IntervalMs { get; init; } = 1000.0f;
}

public sealed class WaveDef
{
    public int DurationSeconds { get; init; } = 30;
    public required string BgmCue { get; init; }
    public SpawnDef[] Spawns { get; init; } = [];
}

public sealed class UpgradeDef
{
    public required string Id { get; init; }
    public required string Name { get; init; }
    public required string Stat { get; init; }
    public float Delta { get; init; }
    public int Weight { get; init; } = 1;
}

public sealed class ShopItemDef
{
    public required string Id { get; init; }
    public required string Name { get; init; }
    public required string Stat { get; init; }
    public float Delta { get; init; }
    public int Cost { get; init; }
    public int Weight { get; init; } = 1;
}

public sealed class ArenaDef
{
    public required string Id { get; init; }
    public required string Name { get; init; }
    public required string ScenePath { get; init; }
    public Vector2 HalfExtent { get; init; } = new(12.0f, 8.0f);
}

/// <summary>AOT-safe manual loader for the same JSON data consumed by the C++ application.</summary>
public sealed class BrotatoDatabase
{
    private const string Root = "assets/configs/brotato3d/";

    public Dictionary<string, EnemyDef> Enemies { get; } = new(StringComparer.Ordinal);
    public Dictionary<string, WeaponDef> Weapons { get; } = new(StringComparer.Ordinal);
    public List<CharacterDef> Characters { get; } = [];
    public List<WaveDef> Waves { get; } = [];
    public List<UpgradeDef> Upgrades { get; } = [];
    public List<ShopItemDef> ShopItems { get; } = [];
    public List<ArenaDef> Arenas { get; } = [];

    public static BrotatoDatabase Load()
    {
        BrotatoDatabase database = new();
        database.LoadEnemies();
        database.LoadWeapons();
        database.LoadCharacters();
        database.LoadWaves();
        database.LoadUpgrades();
        database.LoadShopItems();
        database.LoadArenas();
        if (database.Enemies.Count == 0 || database.Weapons.Count == 0 ||
            database.Characters.Count == 0 || database.Waves.Count == 0 || database.Arenas.Count == 0)
        {
            throw new InvalidDataException("Brotato3DCSharp required configuration is empty");
        }
        return database;
    }

    private void LoadEnemies()
    {
        using JsonDocument document = Open("enemies.json");
        foreach (JsonProperty property in document.RootElement.GetProperty("enemies").EnumerateObject())
        {
            JsonElement value = property.Value;
            EnemyRangedDef ranged = new();
            if (value.TryGetProperty("ranged", out JsonElement rangedJson))
            {
                ranged = new EnemyRangedDef
                {
                    Enabled = true,
                    Damage = Int(rangedJson, "projectileDamage"),
                    Speed = Float(rangedJson, "projectileSpeed"),
                    LifetimeMs = Float(rangedJson, "projectileLifetimeMs"),
                    Color = Vec3(rangedJson, "projectileColor", new Vector3(0.3f, 0.95f, 0.2f)),
                    Size = Float(rangedJson, "projectileSize", 0.18f),
                    IntervalMs = Float(rangedJson, "fireIntervalMs", 1500.0f),
                    PreferredDistance = Float(rangedJson, "preferredDistance", 4.5f),
                };
            }
            Enemies[property.Name] = new EnemyDef
            {
                Id = property.Name,
                Name = String(value, "name", property.Name),
                Hp = Int(value, "hp", 1),
                MoveSpeed = Float(value, "moveSpeed", 1.0f),
                ContactDamage = Int(value, "contactDamage", 1),
                Size = Vec3(value, "size", new Vector3(0.5f, 0.5f, 0.5f)),
                Color = Vec3(value, "color", Vector3.One),
                XpDrop = Int(value, "xpDrop", 1),
                MaterialDrop = Int(value, "materialDrop", 1),
                KitingDistance = Float(value, "kitingDistance"),
                IsBoss = value.TryGetProperty("boss", out _),
                Ranged = ranged,
            };
        }
    }

    private void LoadWeapons()
    {
        using JsonDocument document = Open("weapons.json");
        foreach (JsonProperty property in document.RootElement.GetProperty("weapons").EnumerateObject())
        {
            JsonElement value = property.Value;
            Weapons[property.Name] = new WeaponDef
            {
                Id = property.Name,
                Name = String(value, "name", property.Name),
                Damage = Int(value, "damage", 1),
                AttackSpeedHz = Float(value, "atkSpeedHz", 1.0f),
                RangeMeters = Float(value, "rangeMeters", 1.0f),
                ProjectileSpeed = Float(value, "projectileSpeed", 10.0f),
                ProjectileLifetimeMs = Float(value, "projectileLifetimeMs", 500.0f),
                ProjectileColor = Vec3(value, "projectileColor", Vector3.One),
                ProjectileSize = Float(value, "projectileSize", 0.12f),
                Pellets = Int(value, "pellets", 1),
                SpreadDegrees = Float(value, "spreadDeg"),
                PierceCount = Int(value, "pierceCount"),
                ExplosionRadius = Float(value, "explosionRadius"),
                ExplosionDamage = Int(value, "explosionDamage"),
                InstantHit = Bool(value, "instantHit"),
                CritChanceBonus = Float(value, "critChanceBonus"),
                KnockbackMeters = Float(value, "knockbackMeters"),
            };
        }
    }

    private void LoadCharacters()
    {
        using JsonDocument document = Open("characters.json");
        foreach (JsonElement value in document.RootElement.GetProperty("characters").EnumerateArray())
        {
            PlayerStats stats = PlayerStats.Default;
            if (value.TryGetProperty("startStats", out JsonElement statsJson))
            {
                stats.MaxHpFlat = Float(statsJson, "maxHpFlat", stats.MaxHpFlat);
                stats.DamagePct = Float(statsJson, "damagePct");
                stats.DamageFlat = Float(statsJson, "damageFlat");
                stats.AttackSpeedPct = Float(statsJson, "atkSpeedPct");
                stats.RangePct = Float(statsJson, "rangePct");
                stats.MoveSpeedPct = Float(statsJson, "moveSpeedPct");
                stats.PickupRadiusPct = Float(statsJson, "pickupRadiusPct");
                stats.CritChancePct += Float(statsJson, "critChancePct");
            }
            Characters.Add(new CharacterDef
            {
                Id = String(value, "id", "soldier"),
                Name = String(value, "name", "Soldier"),
                Tagline = String(value, "tagline", string.Empty),
                StartWeapon = String(value, "startWeapon", "smg"),
                Color = Vec3(value, "color", Vector3.One),
                StartStats = stats,
            });
        }
    }

    private void LoadWaves()
    {
        using JsonDocument document = Open("waves.json");
        foreach (JsonElement value in document.RootElement.GetProperty("waves").EnumerateArray())
        {
            List<SpawnDef> spawns = [];
            foreach (JsonElement spawn in value.GetProperty("spawns").EnumerateArray())
            {
                spawns.Add(new SpawnDef
                {
                    EnemyId = String(spawn, "enemyId", "rat"),
                    Count = Int(spawn, "count"),
                    IntervalMs = Float(spawn, "intervalMs", 1000.0f),
                });
            }
            Waves.Add(new WaveDef
            {
                DurationSeconds = Int(value, "durationSec", 30),
                BgmCue = String(value, "bgmCue", "calm"),
                Spawns = [.. spawns],
            });
        }
    }

    private void LoadUpgrades()
    {
        using JsonDocument document = Open("upgrades.json");
        foreach (JsonElement value in document.RootElement.GetProperty("cards").EnumerateArray())
        {
            Upgrades.Add(new UpgradeDef
            {
                Id = String(value, "id", "upgrade"),
                Name = String(value, "name", "Upgrade"),
                Stat = String(value, "stat", string.Empty),
                Delta = Float(value, "delta"),
                Weight = Int(value, "weight", 1),
            });
        }
    }

    private void LoadShopItems()
    {
        using JsonDocument document = Open("shop_items.json");
        foreach (JsonElement value in document.RootElement.GetProperty("items").EnumerateArray())
        {
            ShopItems.Add(new ShopItemDef
            {
                Id = String(value, "id", "shop_item"),
                Name = String(value, "name", "Item"),
                Stat = String(value, "stat", string.Empty),
                Delta = Float(value, "delta"),
                Cost = Int(value, "cost"),
                Weight = Int(value, "weight", 1),
            });
        }
    }

    private void LoadArenas()
    {
        using JsonDocument document = Open("arenas.json");
        foreach (JsonElement value in document.RootElement.GetProperty("arenas").EnumerateArray())
        {
            Arenas.Add(new ArenaDef
            {
                Id = String(value, "id", "arena"),
                Name = String(value, "name", "Arena"),
                ScenePath = String(value, "scene", "Empty.proc"),
                HalfExtent = Vec2(value, "halfExtent", new Vector2(12.0f, 8.0f)),
            });
        }
    }

    private static JsonDocument Open(string filename)
    {
        byte[] data = Assets.ReadFile(Root + filename);
        if (data.Length == 0)
        {
            throw new FileNotFoundException($"Brotato3DCSharp could not read {Root}{filename}");
        }
        return JsonDocument.Parse(data);
    }

    private static string String(JsonElement value, string name, string fallback)
        => value.TryGetProperty(name, out JsonElement property) && property.ValueKind == JsonValueKind.String
               ? property.GetString() ?? fallback
               : fallback;

    private static float Float(JsonElement value, string name, float fallback = 0.0f)
        => value.TryGetProperty(name, out JsonElement property) && property.TryGetSingle(out float result)
               ? result
               : fallback;

    private static int Int(JsonElement value, string name, int fallback = 0)
        => value.TryGetProperty(name, out JsonElement property) && property.TryGetInt32(out int result)
               ? result
               : fallback;

    private static bool Bool(JsonElement value, string name, bool fallback = false)
        => value.TryGetProperty(name, out JsonElement property) &&
           (property.ValueKind == JsonValueKind.True || property.ValueKind == JsonValueKind.False)
               ? property.GetBoolean()
               : fallback;

    private static Vector2 Vec2(JsonElement value, string name, Vector2 fallback)
    {
        if (!value.TryGetProperty(name, out JsonElement property) || property.ValueKind != JsonValueKind.Array ||
            property.GetArrayLength() < 2)
        {
            return fallback;
        }
        return new Vector2(property[0].GetSingle(), property[1].GetSingle());
    }

    private static Vector3 Vec3(JsonElement value, string name, Vector3 fallback)
    {
        if (!value.TryGetProperty(name, out JsonElement property) || property.ValueKind != JsonValueKind.Array ||
            property.GetArrayLength() < 3)
        {
            return fallback;
        }
        return new Vector3(property[0].GetSingle(), property[1].GetSingle(), property[2].GetSingle());
    }
}
