using GkNext;
using GkNext.Interop;

namespace Brotato3D;

public sealed partial class Brotato3DCSharpGameInstance
{
    private void UpdateWeapon(float deltaMs)
    {
        if (!database.Weapons.TryGetValue(equippedWeaponId, out WeaponDef? weapon))
        {
            return;
        }
        weaponCooldownMs -= deltaMs;
        if (weaponCooldownMs > 0.0f)
        {
            return;
        }

        int targetIndex = FindNearestEnemy(weapon.RangeMeters * Math.Max(0.1f, 1.0f + player.Stats.RangePct));
        if (targetIndex < 0)
        {
            weaponCooldownMs = 0.0f;
            return;
        }

        float attackSpeed = weapon.AttackSpeedHz * Math.Max(0.1f, 1.0f + player.Stats.AttackSpeedPct);
        weaponCooldownMs += 1000.0f / Math.Max(0.05f, attackSpeed);
        FireWeapon(weapon, targetIndex);
    }

    private int FindNearestEnemy(float range)
    {
        int nearest = -1;
        float bestDistance = range;
        for (int index = 0; index < enemies.Length; ++index)
        {
            if (!enemies[index].Active)
            {
                continue;
            }
            float distance = BrotatoMath.DistanceXZ(enemies[index].Position, player.Position);
            if (distance <= bestDistance)
            {
                bestDistance = distance;
                nearest = index;
            }
        }
        return nearest;
    }

    private void FireWeapon(WeaponDef weapon, int targetIndex)
    {
        ref EnemyRuntime target = ref enemies[targetIndex];
        Vector3 direction = BrotatoMath.NormalizeXZ(BrotatoMath.Subtract(target.Position, player.Position));
        if (BrotatoMath.LengthXZ(direction) <= 0.001f)
        {
            direction = player.Facing;
        }
        player.Facing = direction;
        int damage = CalculateWeaponDamage(weapon, out bool critical);

        if (weapon.InstantHit)
        {
            DamageEnemy(targetIndex, damage, critical, direction, weapon.KnockbackMeters);
        }
        else
        {
            int pellets = Math.Max(1, weapon.Pellets);
            for (int pellet = 0; pellet < pellets; ++pellet)
            {
                float spreadT = pellets == 1 ? 0.0f : (pellet / (float)(pellets - 1) - 0.5f) * 2.0f;
                float randomJitter = rng.Range(-0.2f, 0.2f);
                float angle = (spreadT + randomJitter) * weapon.SpreadDegrees * (MathF.PI / 180.0f);
                Vector3 pelletDirection = BrotatoMath.RotateY(direction, angle);
                SpawnProjectile(weapon,
                                BrotatoMath.Add(player.Position, BrotatoMath.Multiply(direction, 0.75f)),
                                pelletDirection,
                                damage,
                                enemyOwned: false,
                                weapon.ProjectileSpeed,
                                weapon.ProjectileLifetimeMs,
                                weapon.ProjectileSize,
                                weapon.PierceCount,
                                projectileMaterialIds[weapon.Id]);
            }
        }
        PlayWeaponFireSfx(weapon.Id);
    }

    private int CalculateWeaponDamage(WeaponDef weapon, out bool critical)
    {
        float chance = Math.Clamp(player.Stats.CritChancePct + weapon.CritChanceBonus, 0.0f, 1.0f);
        critical = rng.NextFloat() < chance;
        float damage = weapon.Damage * (1.0f + player.Stats.DamagePct) + player.Stats.DamageFlat;
        if (critical)
        {
            damage *= Math.Max(1.0f, player.Stats.CritMultiplier);
        }
        return Math.Max(1, (int)MathF.Round(damage));
    }

    private bool SpawnProjectile(WeaponDef? weapon,
                                 Vector3 position,
                                 Vector3 direction,
                                 int damage,
                                 bool enemyOwned,
                                 float speed,
                                 float lifetimeMs,
                                 float radius,
                                 int pierce,
                                 uint materialId)
    {
        for (int index = 0; index < projectiles.Length; ++index)
        {
            ref ProjectileRuntime projectile = ref projectiles[index];
            if (projectile.Active)
            {
                continue;
            }
            projectile.Weapon = weapon;
            projectile.Position = position;
            projectile.Position.Y = enemyOwned ? 0.65f : 0.75f;
            projectile.Velocity = BrotatoMath.Multiply(BrotatoMath.NormalizeXZ(direction), speed);
            projectile.LifetimeMs = lifetimeMs;
            projectile.Radius = Math.Max(0.04f, radius);
            projectile.Damage = Math.Max(1, damage);
            projectile.PierceRemaining = Math.Max(0, pierce);
            projectile.LastHitEnemySlot = -1;
            projectile.EnemyOwned = enemyOwned;
            projectile.Active = true;
            Scene.SetNodePrimaryMaterial(projectile.NodeId, materialId);
            Scene.SetNodeVisible(projectile.NodeId, true);
            return true;
        }
        return false;
    }

    private void UpdateProjectiles(float deltaSeconds, float deltaMs)
    {
        for (int index = 0; index < projectiles.Length; ++index)
        {
            ref ProjectileRuntime projectile = ref projectiles[index];
            if (!projectile.Active)
            {
                continue;
            }
            projectile.LifetimeMs -= deltaMs;
            projectile.Position = BrotatoMath.Add(projectile.Position,
                                                   BrotatoMath.Multiply(projectile.Velocity, deltaSeconds));
            if (projectile.LifetimeMs <= 0.0f)
            {
                DeactivateProjectile(ref projectile);
                continue;
            }

            if (projectile.EnemyOwned)
            {
                if (player.DashRemainingMs <= 0.0f &&
                    BrotatoMath.DistanceXZ(projectile.Position, player.Position) <= projectile.Radius + PlayerRadius)
                {
                    DamagePlayer(projectile.Damage);
                    DeactivateProjectile(ref projectile);
                }
                continue;
            }

            for (int enemyIndex = 0; enemyIndex < enemies.Length; ++enemyIndex)
            {
                ref EnemyRuntime enemy = ref enemies[enemyIndex];
                if (!enemy.Active || enemyIndex == projectile.LastHitEnemySlot ||
                    BrotatoMath.DistanceXZ(projectile.Position, enemy.Position) > projectile.Radius + enemy.Radius)
                {
                    continue;
                }
                Vector3 hitDirection = BrotatoMath.NormalizeXZ(projectile.Velocity);
                WeaponDef? weapon = projectile.Weapon;
                float knockback = weapon?.KnockbackMeters ?? 0.0f;
                DamageEnemy(enemyIndex, projectile.Damage, critical: false, hitDirection, knockback);
                if (weapon is { ExplosionRadius: > 0.0f })
                {
                    DamageExplosion(projectile.Position, weapon.ExplosionRadius, weapon.ExplosionDamage, enemyIndex);
                }
                if (projectile.PierceRemaining > 0)
                {
                    --projectile.PierceRemaining;
                    projectile.LastHitEnemySlot = enemyIndex;
                }
                else
                {
                    DeactivateProjectile(ref projectile);
                }
                break;
            }
        }
    }

    private void UpdateEnemies(float deltaSeconds, float deltaMs)
    {
        for (int index = 0; index < enemies.Length; ++index)
        {
            ref EnemyRuntime enemy = ref enemies[index];
            if (!enemy.Active || enemy.Def is not { } definition)
            {
                continue;
            }
            enemy.ContactCooldownMs = Math.Max(0.0f, enemy.ContactCooldownMs - deltaMs);
            enemy.RangedCooldownMs -= deltaMs;
            if (enemy.HitFlashMs > 0.0f)
            {
                enemy.HitFlashMs = Math.Max(0.0f, enemy.HitFlashMs - deltaMs);
                if (enemy.HitFlashMs <= 0.0f)
                {
                    Scene.SetNodePrimaryMaterial(enemy.NodeId, enemyMaterialIds[definition.Id]);
                }
            }

            Vector3 toPlayer = BrotatoMath.Subtract(player.Position, enemy.Position);
            float distance = BrotatoMath.LengthXZ(toPlayer);
            Vector3 direction = BrotatoMath.NormalizeXZ(toPlayer);
            float movement = definition.MoveSpeed;
            if (definition.Ranged.Enabled)
            {
                float preferred = Math.Max(2.0f, definition.Ranged.PreferredDistance);
                if (distance < preferred * 0.78f)
                {
                    direction = BrotatoMath.Multiply(direction, -1.0f);
                }
                else if (distance <= preferred * 1.12f)
                {
                    movement = 0.0f;
                }
                if (enemy.RangedCooldownMs <= 0.0f && distance <= preferred * 1.7f)
                {
                    SpawnEnemyProjectile(enemy, definition.Ranged, BrotatoMath.NormalizeXZ(toPlayer));
                    enemy.RangedCooldownMs = definition.Ranged.IntervalMs;
                }
            }
            enemy.Position = BrotatoMath.Add(enemy.Position,
                                              BrotatoMath.Multiply(direction, movement * deltaSeconds));
            enemy.Position = BrotatoMath.ClampToArena(enemy.Position, enemy.Radius, CurrentArena.HalfExtent);
            enemy.Position.Y = 0.0f;

            if (player.DashRemainingMs <= 0.0f && enemy.ContactCooldownMs <= 0.0f &&
                distance <= PlayerRadius + enemy.Radius)
            {
                DamagePlayer(definition.ContactDamage);
                enemy.ContactCooldownMs = 600.0f;
            }
        }
    }

    private void SpawnEnemyProjectile(in EnemyRuntime enemy, EnemyRangedDef ranged, Vector3 direction)
    {
        uint material = enemy.Def is not null ? enemyMaterialIds[enemy.Def.Id] : whiteMaterialId;
        SpawnProjectile(null,
                        enemy.Position,
                        direction,
                        ranged.Damage,
                        enemyOwned: true,
                        ranged.Speed,
                        ranged.LifetimeMs,
                        ranged.Size,
                        0,
                        material);
    }

    private void DamageEnemy(int enemyIndex, int damage, bool critical, Vector3 hitDirection, float knockback)
    {
        ref EnemyRuntime enemy = ref enemies[enemyIndex];
        if (!enemy.Active)
        {
            return;
        }
        enemy.CurrentHp -= Math.Max(1, damage);
        enemy.HitFlashMs = 55.0f;
        Scene.SetNodePrimaryMaterial(enemy.NodeId, whiteMaterialId);
        if (knockback > 0.0f)
        {
            enemy.Position = BrotatoMath.ClampToArena(
                BrotatoMath.Add(enemy.Position, BrotatoMath.Multiply(hitDirection, knockback)),
                enemy.Radius,
                CurrentArena.HalfExtent);
        }
        if (enemy.CurrentHp <= 0)
        {
            KillEnemy(enemyIndex, dropLoot: true);
        }
        PlayHitSfx(critical);
    }

    private void DamageExplosion(Vector3 center, float radius, int damage, int ignoredEnemyIndex)
    {
        if (radius <= 0.0f || damage <= 0)
        {
            return;
        }
        for (int index = 0; index < enemies.Length; ++index)
        {
            if (index == ignoredEnemyIndex || !enemies[index].Active ||
                BrotatoMath.DistanceXZ(enemies[index].Position, center) > radius)
            {
                continue;
            }
            Vector3 direction = BrotatoMath.NormalizeXZ(BrotatoMath.Subtract(enemies[index].Position, center));
            DamageEnemy(index, damage, critical: false, direction, 0.08f);
        }
        StartCameraShake(160.0f, 2.5f);
    }

    private void KillEnemy(int enemyIndex, bool dropLoot)
    {
        ref EnemyRuntime enemy = ref enemies[enemyIndex];
        if (!enemy.Active)
        {
            return;
        }
        EnemyDef? definition = enemy.Def;
        Vector3 position = enemy.Position;
        enemy.Active = false;
        DeactivateEnemyPushBody(ref enemy);
        Scene.SetNodeVisible(enemy.NodeId, false);
        if (!dropLoot || definition is null)
        {
            return;
        }
        ++killCount;
        SpawnDeathDebris(position, enemyMaterialIds[definition.Id], definition.IsBoss);
        SpawnPickup(position, definition.XpDrop, definition.MaterialDrop);
        PlayEnemyDeathSfx(definition.Id);
        if (definition.IsBoss)
        {
            StartCameraShake(800.0f, 5.0f);
        }
    }

    private void SpawnPickup(Vector3 position, int xp, int materials)
    {
        for (int index = 0; index < pickups.Length; ++index)
        {
            ref PickupRuntime pickup = ref pickups[index];
            if (pickup.Active)
            {
                continue;
            }
            pickup.Position = position;
            pickup.Position.Y = 0.22f;
            pickup.Xp = Math.Max(0, xp);
            pickup.Materials = Math.Max(0, materials);
            pickup.Active = true;
            Scene.SetNodePrimaryMaterial(pickup.NodeId,
                                         pickup.Materials > 0 ? materialPickupMaterialId : xpMaterialId);
            Scene.SetNodeVisible(pickup.NodeId, true);
            return;
        }
    }

    private void UpdatePickups(float deltaSeconds)
    {
        float pickupRadius = 1.6f * Math.Max(0.1f, 1.0f + player.Stats.PickupRadiusPct);
        for (int index = 0; index < pickups.Length; ++index)
        {
            ref PickupRuntime pickup = ref pickups[index];
            if (!pickup.Active)
            {
                continue;
            }
            Vector3 toPlayer = BrotatoMath.Subtract(player.Position, pickup.Position);
            float distance = BrotatoMath.LengthXZ(toPlayer);
            if (distance <= pickupRadius)
            {
                float speed = pickup.Materials > 0 ? 12.0f : 8.0f;
                pickup.Position = BrotatoMath.Add(pickup.Position,
                    BrotatoMath.Multiply(BrotatoMath.NormalizeXZ(toPlayer), speed * deltaSeconds));
            }
            if (distance <= 0.45f)
            {
                AddExperience(pickup.Xp);
                player.Materials += pickup.Materials;
                pickup.Active = false;
                Scene.SetNodeVisible(pickup.NodeId, false);
                PlaySfx(pickup.Materials > 0 ? "pickup_material.wav" : "pickup_xp_01.ogg", 0.55f, 45);
            }
        }
    }

    private void DamagePlayer(int damage)
    {
        player.CurrentHp = Math.Max(0, player.CurrentHp - Math.Max(1, damage));
        StartCameraShake(150.0f, 3.0f);
        PlaySfx("player_hurt_01.wav", 0.75f, 120);
    }

    private void DeactivateProjectile(ref ProjectileRuntime projectile)
    {
        projectile.Active = false;
        projectile.Weapon = null;
        Scene.SetNodeVisible(projectile.NodeId, false);
    }

    private void ClearProjectiles()
    {
        for (int index = 0; index < projectiles.Length; ++index)
        {
            if (projectiles[index].Active)
            {
                DeactivateProjectile(ref projectiles[index]);
            }
        }
    }

    private void ClearEnemies(bool dropLoot)
    {
        for (int index = 0; index < enemies.Length; ++index)
        {
            if (enemies[index].Active)
            {
                KillEnemy(index, dropLoot);
            }
        }
    }

    private void PlayWeaponFireSfx(string weaponId)
    {
        string filename = weaponId switch
        {
            "shotgun" => "fire_shotgun_01.wav",
            "sniper" => "fire_sniper_01.wav",
            "flamethrower" => "fire_flamethrower_01.wav",
            "rocket" => "fire_rocket_01.wav",
            "laser" => "fire_laser_01.wav",
            _ => "fire_smg_01.wav",
        };
        PlaySfx(filename, 0.55f, weaponId == "flamethrower" ? 40u : 55u);
    }

    private void PlayHitSfx(bool critical)
        => PlaySfx(critical ? "hit_crit_01.wav" : "hit_normal_01.wav", critical ? 0.75f : 0.50f, 35);

    private void PlayEnemyDeathSfx(string enemyId)
        => PlaySfx(enemyId == "boss_warden" ? "enemy_die_boss.wav" :
                   enemyId == "tank" ? "enemy_die_tank_01.wav" : "enemy_die_small_01.wav",
                   enemyId == "boss_warden" ? 0.95f : 0.6f,
                   55);
}
