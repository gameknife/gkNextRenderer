using GkNext;
using GkNext.Interop;

namespace Brotato3D;

public sealed partial class Brotato3DCSharpGameInstance
{
    private static readonly Vector3 hiddenPosition = new(0.0f, HiddenY, 0.0f);
    private static readonly Vector4 identityRotation = new(0.0f, 0.0f, 0.0f, 1.0f);

    private void BuildManagedScene()
    {
        enemyMaterialIds.Clear();
        projectileMaterialIds.Clear();
        characterMaterialIds.Clear();

        Vector3 white = Vector3.One;
        Vector3 xpColor = new(0.24f, 0.72f, 1.0f);
        Vector3 materialColor = new(1.0f, 0.78f, 0.12f);
        whiteMaterialId = SceneBuild.AddLambertianMaterial(in white);
        xpMaterialId = SceneBuild.AddLambertianMaterial(in xpColor);
        materialPickupMaterialId = SceneBuild.AddLambertianMaterial(in materialColor);

        foreach (CharacterDef character in database.Characters)
        {
            Vector3 color = character.Color;
            characterMaterialIds[character.Id] = SceneBuild.AddLambertianMaterial(in color);
        }
        foreach ((string id, EnemyDef enemy) in database.Enemies)
        {
            Vector3 color = enemy.Color;
            enemyMaterialIds[id] = SceneBuild.AddLambertianMaterial(in color);
        }
        foreach ((string id, WeaponDef weapon) in database.Weapons)
        {
            Vector3 color = weapon.ProjectileColor;
            projectileMaterialIds[id] = SceneBuild.AddLambertianMaterial(in color);
        }

        Vector3 sphereCenter = Vector3.Zero;
        uint playerModel = SceneBuild.AddSphereModel(in sphereCenter, 0.6f);
        uint projectileModel = SceneBuild.AddSphereModel(in sphereCenter, 1.0f);
        uint pickupModel = SceneBuild.AddSphereModel(in sphereCenter, 1.0f);
        Vector3 enemyMin = new(-0.5f, 0.0f, -0.5f);
        Vector3 enemyMax = new(0.5f, 1.0f, 0.5f);
        uint enemyModel = SceneBuild.AddBoxModel(in enemyMin, in enemyMax);
        Vector3 weaponMin = new(-0.08f, -0.06f, -0.36f);
        Vector3 weaponMax = new(0.08f, 0.06f, 0.36f);
        uint weaponModel = SceneBuild.AddBoxModel(in weaponMin, in weaponMax);
        Vector3 tinyDebrisMin = new(-0.08f, -0.08f, -0.08f);
        Vector3 tinyDebrisMax = new(0.08f, 0.08f, 0.08f);
        Vector3 chunkDebrisMin = new(-0.18f, -0.18f, -0.18f);
        Vector3 chunkDebrisMax = new(0.18f, 0.18f, 0.18f);
        Vector3 bossDebrisMin = new(-0.36f, -0.36f, -0.36f);
        Vector3 bossDebrisMax = new(0.36f, 0.36f, 0.36f);
        uint tinyDebrisModel = SceneBuild.AddBoxModel(in tinyDebrisMin, in tinyDebrisMax);
        uint chunkDebrisModel = SceneBuild.AddBoxModel(in chunkDebrisMin, in chunkDebrisMax);
        uint bossDebrisModel = SceneBuild.AddBoxModel(in bossDebrisMin, in bossDebrisMax);

        playerMaterialId = characterMaterialIds[database.Characters[selectedCharacterIndex].Id];
        RenderNodeSpec playerSpec = new RenderNodeSpec(playerModel, playerMaterialId)
            .WithTranslation(hiddenPosition)
            .WithVisible(false);
        playerNodeId = SceneBuild.AddRenderNode("Brotato3DCSharp_Player", in playerSpec);

        Vector3 weaponColor = new(1.0f, 0.82f, 0.18f);
        uint weaponMaterial = SceneBuild.AddLambertianMaterial(in weaponColor);
        RenderNodeSpec weaponSpec = new RenderNodeSpec(weaponModel, weaponMaterial)
            .WithTranslation(hiddenPosition)
            .WithVisible(false);
        weaponNodeId = SceneBuild.AddRenderNode("Brotato3DCSharp_PlayerWeapon", in weaponSpec);

        uint firstEnemyMaterial = enemyMaterialIds.Count > 0 ? enemyMaterialIds.Values.First() : whiteMaterialId;
        for (int index = 0; index < enemies.Length; ++index)
        {
            RenderNodeSpec spec = new RenderNodeSpec(enemyModel, firstEnemyMaterial)
                .WithTranslation(hiddenPosition)
                .WithVisible(false);
            enemies[index].NodeId = SceneBuild.AddRenderNode($"Brotato3DCSharp_Enemy_{index}", in spec);
        }

        uint firstProjectileMaterial = projectileMaterialIds.Count > 0 ? projectileMaterialIds.Values.First() : whiteMaterialId;
        for (int index = 0; index < projectiles.Length; ++index)
        {
            RenderNodeSpec spec = new RenderNodeSpec(projectileModel, firstProjectileMaterial)
                .WithTranslation(hiddenPosition)
                .WithScale(new Vector3(0.12f, 0.12f, 0.12f))
                .WithVisible(false);
            projectiles[index].NodeId = SceneBuild.AddRenderNode($"Brotato3DCSharp_Projectile_{index}", in spec);
        }

        for (int index = 0; index < pickups.Length; ++index)
        {
            RenderNodeSpec spec = new RenderNodeSpec(pickupModel, xpMaterialId)
                .WithTranslation(hiddenPosition)
                .WithScale(new Vector3(0.12f, 0.12f, 0.12f))
                .WithVisible(false);
            pickups[index].NodeId = SceneBuild.AddRenderNode($"Brotato3DCSharp_Pickup_{index}", in spec);
        }

        BuildPhysicsPools(playerModel,
                          enemyModel,
                          tinyDebrisModel,
                          chunkDebrisModel,
                          bossDebrisModel,
                          firstEnemyMaterial);
    }

    private void SubmitTransforms()
    {
        int count = 0;
        Vector4 facingRotation = BrotatoMath.RotationFromDirection(player.Facing);
        transformBuffer[count++] = new NodeTransform(playerNodeId, player.Position, facingRotation, Vector3.One);

        Vector3 weaponPosition = BrotatoMath.Add(player.Position,
            BrotatoMath.Add(BrotatoMath.Multiply(player.Facing, 0.72f), new Vector3(0.0f, 0.16f, 0.0f)));
        transformBuffer[count++] = new NodeTransform(weaponNodeId, weaponPosition, facingRotation, Vector3.One);

        for (int index = 0; index < enemies.Length; ++index)
        {
            ref EnemyRuntime enemy = ref enemies[index];
            if (!enemy.Active || enemy.Def is null)
            {
                continue;
            }
            transformBuffer[count++] = new NodeTransform(enemy.NodeId,
                                                         enemy.Position,
                                                         identityRotation,
                                                         enemy.Def.Size);
        }
        for (int index = 0; index < projectiles.Length; ++index)
        {
            ref ProjectileRuntime projectile = ref projectiles[index];
            if (!projectile.Active)
            {
                continue;
            }
            Vector3 scale = new(projectile.Radius, projectile.Radius, projectile.Radius);
            transformBuffer[count++] = new NodeTransform(projectile.NodeId,
                                                         projectile.Position,
                                                         identityRotation,
                                                         scale);
        }
        float bobTime = (float)Engine.GetTime() * 3.0f;
        for (int index = 0; index < pickups.Length; ++index)
        {
            ref PickupRuntime pickup = ref pickups[index];
            if (!pickup.Active)
            {
                continue;
            }
            Vector3 position = pickup.Position;
            position.Y += 0.08f * MathF.Sin(bobTime + index * 0.37f);
            Vector3 scale = pickup.Materials > 0 ? new Vector3(0.16f, 0.16f, 0.16f) : new Vector3(0.11f, 0.11f, 0.11f);
            transformBuffer[count++] = new NodeTransform(pickup.NodeId, position, identityRotation, scale);
        }

        Scene.SetNodeTransforms(transformBuffer.AsSpan(0, count));
        Scene.MarkTransformDirty();

        float cameraLerp = 1.0f - MathF.Exp(-8.0f * Math.Max(0.0f, (float)Engine.GetDeltaSeconds()));
        Vector3 desiredTarget = new(player.Position.X, 0.0f, player.Position.Z);
        cameraTarget = BrotatoMath.Add(cameraTarget,
                                      BrotatoMath.Multiply(BrotatoMath.Subtract(desiredTarget, cameraTarget), cameraLerp));
    }

    private void HideAllPoolNodes()
    {
        for (int index = 0; index < enemies.Length; ++index)
        {
            if (NodeIds.IsValid(enemies[index].NodeId))
            {
                Scene.SetNodeVisible(enemies[index].NodeId, false);
            }
        }
        for (int index = 0; index < projectiles.Length; ++index)
        {
            if (NodeIds.IsValid(projectiles[index].NodeId))
            {
                Scene.SetNodeVisible(projectiles[index].NodeId, false);
            }
        }
        for (int index = 0; index < pickups.Length; ++index)
        {
            if (NodeIds.IsValid(pickups[index].NodeId))
            {
                Scene.SetNodeVisible(pickups[index].NodeId, false);
            }
        }
    }
}
