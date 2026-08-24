using GkNext;
using GkNext.Interop;

namespace Brotato3D;

public sealed partial class Brotato3DCSharpGameInstance
{
    private void StartWave(int waveIndex)
    {
        if (waveIndex < 0 || waveIndex >= database.Waves.Count)
        {
            EnterResult(victory: true);
            return;
        }
        WaveDef wave = database.Waves[waveIndex];
        waveRemainingSeconds = wave.DurationSeconds;
        activeSpawnEntryCount = Math.Min(wave.Spawns.Length, spawnEntries.Length);
        for (int index = 0; index < spawnEntries.Length; ++index)
        {
            if (index < activeSpawnEntryCount)
            {
                SpawnDef spawn = wave.Spawns[index];
                spawnEntries[index] = new SpawnRuntime
                {
                    Def = spawn,
                    Remaining = spawn.Count,
                    TimerMs = spawn.IntervalMs,
                };
            }
            else
            {
                spawnEntries[index] = default;
            }
        }
        PlaySfx(waveIndex >= database.Waves.Count - 1 ? "wave_start_boss.wav" : "wave_start.wav", 0.8f, 0);
    }

    private void UpdateWave(float deltaSeconds, float deltaMs)
    {
        waveRemainingSeconds = Math.Max(0.0f, waveRemainingSeconds - deltaSeconds);
        for (int index = 0; index < activeSpawnEntryCount; ++index)
        {
            ref SpawnRuntime spawn = ref spawnEntries[index];
            if (spawn.Def is null || spawn.Remaining <= 0)
            {
                continue;
            }
            spawn.TimerMs -= deltaMs;
            int catchUpGuard = 0;
            while (spawn.TimerMs <= 0.0f && spawn.Remaining > 0 && catchUpGuard++ < 4)
            {
                if (SpawnEnemy(spawn.Def.EnemyId))
                {
                    --spawn.Remaining;
                }
                spawn.TimerMs += Math.Max(20.0f, spawn.Def.IntervalMs);
            }
        }

        if (waveRemainingSeconds > 0.0f)
        {
            return;
        }
        if (currentWaveIndex >= database.Waves.Count - 1)
        {
            EnterResult(victory: true);
            return;
        }
        OpenShop();
    }

    private bool SpawnEnemy(string enemyId)
    {
        if (!database.Enemies.TryGetValue(enemyId, out EnemyDef? definition))
        {
            return false;
        }
        int slot = -1;
        for (int index = 0; index < enemies.Length; ++index)
        {
            if (!enemies[index].Active)
            {
                slot = index;
                break;
            }
        }
        if (slot < 0)
        {
            return false;
        }

        float angle = rng.Range(0.0f, MathF.PI * 2.0f);
        float distance = rng.Range(42.0f, 50.0f);
        Vector3 position = BrotatoMath.Add(player.Position,
            new Vector3(MathF.Cos(angle) * distance, 0.0f, MathF.Sin(angle) * distance));
        float radius = Math.Max(definition.Size.X, definition.Size.Z) * 0.5f;
        position = BrotatoMath.ClampToArena(position, radius, CurrentArena.HalfExtent);
        position.Y = 0.0f;

        ref EnemyRuntime enemy = ref enemies[slot];
        enemy.Def = definition;
        enemy.Position = position;
        enemy.CurrentHp = definition.Hp;
        enemy.Radius = radius;
        enemy.ContactCooldownMs = 0.0f;
        enemy.RangedCooldownMs = definition.Ranged.Enabled ? rng.Range(200.0f, definition.Ranged.IntervalMs) : 0.0f;
        enemy.HitFlashMs = 0.0f;
        enemy.Active = true;
        ActivateEnemyPushBody(ref enemy);
        Scene.SetNodePrimaryMaterial(enemy.NodeId, enemyMaterialIds[definition.Id]);
        Scene.SetNodeVisible(enemy.NodeId, true);
        return true;
    }

    private void OpenShop()
    {
        ClearEnemies(dropLoot: false);
        ClearProjectiles();
        RollShopOffers();
        SetAppState(AppState.Shopping);
        PlaySfx("shop_open.wav", 0.75f, 0);
    }

    private void RollShopOffers()
    {
        Array.Clear(shopOffers);
        for (int slot = 0; slot < shopOffers.Length && slot < database.ShopItems.Count; ++slot)
        {
            for (int attempt = 0; attempt < 32; ++attempt)
            {
                ShopItemDef candidate = database.ShopItems[rng.NextInt(database.ShopItems.Count)];
                bool duplicate = false;
                for (int previous = 0; previous < slot; ++previous)
                {
                    duplicate |= shopOffers[previous]?.Id == candidate.Id;
                }
                if (!duplicate)
                {
                    shopOffers[slot] = candidate;
                    break;
                }
            }
        }
    }

    private void BuyShopOffer(int slotIndex)
    {
        if (slotIndex < 0 || slotIndex >= shopOffers.Length || shopOffers[slotIndex] is not { } offer)
        {
            return;
        }
        if (player.Materials < offer.Cost)
        {
            PlaySfx("shop_cant_buy.wav", 0.7f, 45);
            return;
        }
        player.Materials -= offer.Cost;
        ApplyStat(offer.Stat, offer.Delta);
        shopOffers[slotIndex] = null;
        PlaySfx("shop_buy.wav", 0.8f, 45);
    }

    private void ContinueFromShop()
    {
        ++currentWaveIndex;
        if (currentWaveIndex >= database.Waves.Count)
        {
            EnterResult(victory: true);
            return;
        }
        SetAppState(AppState.Playing);
        StartWave(currentWaveIndex);
    }
}
