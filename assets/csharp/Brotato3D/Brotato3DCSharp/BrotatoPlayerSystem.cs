using GkNext;
using GkNext.Interop;

namespace Brotato3D;

public sealed partial class Brotato3DCSharpGameInstance
{
    private void UpdatePlayer(float deltaSeconds, float deltaMs)
    {
        int maxDashCharges = 3;
        if (player.DashCharges < maxDashCharges)
        {
            player.DashCooldownMs -= deltaMs;
            while (player.DashCooldownMs <= 0.0f && player.DashCharges < maxDashCharges)
            {
                ++player.DashCharges;
                player.DashCooldownMs = player.DashCharges < maxDashCharges ? PlayerDashCooldownMs : 0.0f;
            }
        }
        else
        {
            player.DashCooldownMs = 0.0f;
        }

        Vector3 input = ResolveMovementInput();
        AimAtNearestEnemy(deltaSeconds, input);
        if (player.DashRemainingMs > 0.0f)
        {
            float stepMs = Math.Min(player.DashRemainingMs, deltaMs);
            player.DashRemainingMs = Math.Max(0.0f, player.DashRemainingMs - deltaMs);
            player.Position = BrotatoMath.Add(player.Position,
                BrotatoMath.Multiply(player.DashDirection, PlayerDashSpeed * stepMs / 1000.0f));
        }
        else
        {
            float speed = PlayerBaseSpeed * Math.Max(0.1f, 1.0f + player.Stats.MoveSpeedPct);
            player.Position = BrotatoMath.Add(player.Position, BrotatoMath.Multiply(input, speed * deltaSeconds));
        }
        player.Position = BrotatoMath.ClampToArena(player.Position, PlayerRadius, CurrentArena.HalfExtent);
        player.Position.Y = PlayerRadius;
    }

    private Vector3 ResolveMovementInput()
    {
        float x = (Input.IsKeyDown("d") ? 1.0f : 0.0f) - (Input.IsKeyDown("a") ? 1.0f : 0.0f);
        float z = (Input.IsKeyDown("s") ? 1.0f : 0.0f) - (Input.IsKeyDown("w") ? 1.0f : 0.0f);
        float gamepadX = Input.GetGamepadAxis(0);
        float gamepadY = Input.GetGamepadAxis(1);
        if (MathF.Sqrt(gamepadX * gamepadX + gamepadY * gamepadY) > 0.10f)
        {
            x += gamepadX;
            z += gamepadY;
        }
        Vector3 input = BrotatoMath.NormalizeXZ(new Vector3(x, 0.0f, z));
        return input;
    }

    private void AimAtNearestEnemy(float deltaSeconds, Vector3 movementInput)
    {
        float range = database.Weapons.TryGetValue(equippedWeaponId, out WeaponDef? weapon)
                          ? weapon.RangeMeters * Math.Max(0.1f, 1.0f + player.Stats.RangePct)
                          : 8.0f;
        float bestDistance = float.MaxValue;
        Vector3 bestDirection = Vector3.Zero;
        for (int index = 0; index < enemies.Length; ++index)
        {
            ref EnemyRuntime enemy = ref enemies[index];
            if (!enemy.Active)
            {
                continue;
            }
            float distance = BrotatoMath.DistanceXZ(enemy.Position, player.Position);
            if (distance < bestDistance && distance <= range)
            {
                bestDistance = distance;
                bestDirection = BrotatoMath.NormalizeXZ(BrotatoMath.Subtract(enemy.Position, player.Position));
            }
        }
        if (bestDistance < float.MaxValue)
        {
            float lerp = 1.0f - MathF.Exp(-14.0f * deltaSeconds);
            player.Facing = BrotatoMath.NormalizeXZ(BrotatoMath.Add(
                BrotatoMath.Multiply(player.Facing, 1.0f - lerp),
                BrotatoMath.Multiply(bestDirection, lerp)));
        }
        else if (BrotatoMath.LengthXZ(movementInput) > 0.001f)
        {
            player.Facing = movementInput;
        }
    }

    private void TryStartDash()
    {
        if (player.DashRemainingMs > 0.0f || player.DashCharges <= 0)
        {
            return;
        }
        Vector3 direction = ResolveMovementInput();
        if (BrotatoMath.LengthXZ(direction) <= 0.001f)
        {
            direction = player.Facing;
        }
        player.DashDirection = BrotatoMath.NormalizeXZ(direction);
        player.DashRemainingMs = PlayerDashDurationMs;
        --player.DashCharges;
        if (player.DashCharges == 2)
        {
            player.DashCooldownMs = PlayerDashCooldownMs;
        }
        PlaySfx("ui_click.wav", 0.45f, 35);
    }

    private int XpToNextLevel() => 10 + Math.Max(0, player.Level - 1) * 8;

    private void AddExperience(int amount)
    {
        player.CurrentXp += Math.Max(0, amount);
        while (player.CurrentXp >= XpToNextLevel())
        {
            player.CurrentXp -= XpToNextLevel();
            ++player.Level;
            ++player.PendingLevelUps;
        }
        if (player.PendingLevelUps > 0 && state == AppState.Playing)
        {
            RollUpgradeChoices();
            SetAppState(AppState.LevelUpPicking);
            PlaySfx("level_up.wav", 0.9f, 0);
        }
    }

    private void RollUpgradeChoices()
    {
        Array.Clear(upgradeChoices);
        for (int slot = 0; slot < upgradeChoices.Length && slot < database.Upgrades.Count; ++slot)
        {
            for (int attempt = 0; attempt < 32; ++attempt)
            {
                UpgradeDef candidate = database.Upgrades[rng.NextInt(database.Upgrades.Count)];
                bool duplicate = false;
                for (int previous = 0; previous < slot; ++previous)
                {
                    duplicate |= upgradeChoices[previous]?.Id == candidate.Id;
                }
                if (!duplicate)
                {
                    upgradeChoices[slot] = candidate;
                    break;
                }
            }
        }
    }

    private void SelectUpgrade(int choiceIndex)
    {
        if (choiceIndex < 0 || choiceIndex >= upgradeChoices.Length || upgradeChoices[choiceIndex] is not { } choice)
        {
            return;
        }
        ApplyStat(choice.Stat, choice.Delta);
        player.PendingLevelUps = Math.Max(0, player.PendingLevelUps - 1);
        if (player.PendingLevelUps > 0)
        {
            RollUpgradeChoices();
        }
        else
        {
            SetAppState(AppState.Playing);
        }
        PlaySfx("ui_click.wav", 0.6f, 35);
    }

    private void ApplyStat(string stat, float delta)
    {
        switch (stat)
        {
        case "damagePct":
            player.Stats.DamagePct += delta;
            break;
        case "damageFlat":
            player.Stats.DamageFlat += delta;
            break;
        case "atkSpeedPct":
            player.Stats.AttackSpeedPct += delta;
            break;
        case "rangePct":
            player.Stats.RangePct += delta;
            break;
        case "moveSpeedPct":
            player.Stats.MoveSpeedPct += delta;
            break;
        case "pickupRadiusPct":
            player.Stats.PickupRadiusPct += delta;
            break;
        case "critChancePct":
            player.Stats.CritChancePct += delta;
            break;
        case "critMultiplier":
            player.Stats.CritMultiplier += delta;
            break;
        case "maxHpFlat":
            int hpIncrease = Math.Max(1, (int)MathF.Round(delta));
            player.Stats.MaxHpFlat += hpIncrease;
            player.MaxHp += hpIncrease;
            player.CurrentHp += hpIncrease;
            break;
        case "healPct":
            player.CurrentHp = Math.Min(player.MaxHp,
                player.CurrentHp + Math.Max(1, (int)MathF.Round(player.MaxHp * delta)));
            break;
        default:
            Log.Warn($"[Brotato3DCSharp] unsupported stat '{stat}'");
            break;
        }
    }
}
