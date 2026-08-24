using GkNext;
using GkNext.Interop;

namespace Brotato3D;

public sealed partial class Brotato3DCSharpGameInstance
{
    private const float KinematicMoveStepSeconds = 1.0f / 60.0f;

    private void BuildPhysicsPools(uint playerProxyModel,
                                   uint enemyProxyModel,
                                   uint tinyDebrisModel,
                                   uint chunkDebrisModel,
                                   uint bossDebrisModel,
                                   uint fallbackMaterial)
    {
        physicsPoolsBuilt = false;
        playerPushBodyId = PhysicsBodyIds.Invalid;
        playerPushBodyActive = false;
        for (int index = 0; index < arenaWallBodyIds.Length; ++index)
        {
            arenaWallBodyIds[index] = PhysicsBodyIds.Invalid;
        }
        for (int index = 0; index < enemies.Length; ++index)
        {
            enemies[index].PushBodyId = PhysicsBodyIds.Invalid;
            enemies[index].PushBodyActive = false;
        }
        for (int index = 0; index < debris.Length; ++index)
        {
            debris[index].NodeId = NodeIds.Invalid;
            debris[index].BodyId = PhysicsBodyIds.Invalid;
            debris[index].Active = false;
        }

        if (!Physics.IsAvailable())
        {
            Log.Warn("[Brotato3DCSharp] physics module unavailable; debris visuals disabled");
            return;
        }

        playerPushBodyId = Physics.CreateSphereBody(in hiddenPosition,
                                                     PlayerRadius,
                                                     PhysicsMotionType.Kinematic);
        if (PhysicsBodyIds.IsValid(playerPushBodyId))
        {
            Physics.SetBodyActive(playerPushBodyId, false);
            if (!AddPhysicsProxyNode("Brotato3DCSharp_PlayerPushProxy",
                                     playerProxyModel,
                                     fallbackMaterial,
                                     playerPushBodyId))
            {
                playerPushBodyId = PhysicsBodyIds.Invalid;
            }
        }

        Vector3 enemyPushExtent = new(1.0f, 1.0f, 1.0f);
        for (int index = 0; index < enemies.Length; ++index)
        {
            uint bodyId = Physics.CreateBoxBody(in hiddenPosition,
                                                in identityRotation,
                                                in enemyPushExtent,
                                                PhysicsMotionType.Kinematic);
            enemies[index].PushBodyId = bodyId;
            if (!PhysicsBodyIds.IsValid(bodyId))
            {
                continue;
            }
            Physics.SetBodyActive(bodyId, false);
            if (!AddPhysicsProxyNode($"Brotato3DCSharp_EnemyPushProxy_{index}",
                                     enemyProxyModel,
                                     fallbackMaterial,
                                     bodyId))
            {
                enemies[index].PushBodyId = PhysicsBodyIds.Invalid;
            }
        }

        Vector3 tinyExtent = new(0.16f, 0.16f, 0.16f);
        Vector3 chunkExtent = new(0.36f, 0.36f, 0.36f);
        Vector3 bossExtent = new(0.72f, 0.72f, 0.72f);
        for (int index = 0; index < debris.Length; ++index)
        {
            uint modelId;
            Vector3 extent;
            string prefix;
            if (index < TinyDebrisCount)
            {
                modelId = tinyDebrisModel;
                extent = tinyExtent;
                prefix = "Tiny";
            }
            else if (index < TinyDebrisCount + ChunkDebrisCount)
            {
                modelId = chunkDebrisModel;
                extent = chunkExtent;
                prefix = "Chunk";
            }
            else
            {
                modelId = bossDebrisModel;
                extent = bossExtent;
                prefix = "BossChunk";
            }

            uint bodyId = Physics.CreateBoxBody(in hiddenPosition,
                                                in identityRotation,
                                                in extent,
                                                PhysicsMotionType.Dynamic);
            RenderNodeSpec spec = new RenderNodeSpec(modelId, fallbackMaterial)
                .WithTranslation(hiddenPosition)
                .WithVisible(false);
            uint nodeId = SceneBuild.AddRenderNode($"Brotato3DCSharp_Debris{prefix}_{index}", in spec);
            debris[index].NodeId = nodeId;
            debris[index].BodyId = bodyId;

            if (!NodeIds.IsValid(nodeId))
            {
                if (PhysicsBodyIds.IsValid(bodyId))
                {
                    Physics.RemoveBody(bodyId);
                    debris[index].BodyId = PhysicsBodyIds.Invalid;
                }
                continue;
            }
            if (!PhysicsBodyIds.IsValid(bodyId))
            {
                continue;
            }
            Physics.SetBodyActive(bodyId, false);
            if (!SceneBuild.BindPhysicsBody(nodeId, bodyId, NodeMobility.Dynamic))
            {
                Physics.RemoveBody(bodyId);
                debris[index].BodyId = PhysicsBodyIds.Invalid;
            }
        }

        BuildArenaWallBodies();
        physicsPoolsBuilt = true;
        Log.Info($"[Brotato3DCSharp] physics pools created: {debris.Length} debris + {enemies.Length + 1} push bodies");
    }

    private bool AddPhysicsProxyNode(string name, uint modelId, uint materialId, uint bodyId)
    {
        RenderNodeSpec spec = new RenderNodeSpec(modelId, materialId)
            .WithTranslation(hiddenPosition)
            .WithVisible(false);
        uint nodeId = SceneBuild.AddRenderNode(name, in spec);
        if (!NodeIds.IsValid(nodeId) || !SceneBuild.BindPhysicsBody(nodeId, bodyId, NodeMobility.Dynamic))
        {
            Physics.RemoveBody(bodyId);
            return false;
        }
        return true;
    }

    private void BuildArenaWallBodies()
    {
        const float height = 5.0f;
        const float thickness = 0.5f;
        Vector2 halfExtent = CurrentArena.HalfExtent;
        Vector3 sideExtent = new(thickness, height, halfExtent.Y * 2.0f + thickness * 2.0f);
        Vector3 endExtent = new(halfExtent.X * 2.0f + thickness * 2.0f, height, thickness);
        Vector3 left = new(-halfExtent.X - thickness * 0.5f, height * 0.5f, 0.0f);
        Vector3 right = new(halfExtent.X + thickness * 0.5f, height * 0.5f, 0.0f);
        Vector3 near = new(0.0f, height * 0.5f, -halfExtent.Y - thickness * 0.5f);
        Vector3 far = new(0.0f, height * 0.5f, halfExtent.Y + thickness * 0.5f);

        arenaWallBodyIds[0] = Physics.CreateBoxBody(in left, in identityRotation, in sideExtent,
                                                    PhysicsMotionType.Static);
        arenaWallBodyIds[1] = Physics.CreateBoxBody(in right, in identityRotation, in sideExtent,
                                                    PhysicsMotionType.Static);
        arenaWallBodyIds[2] = Physics.CreateBoxBody(in near, in identityRotation, in endExtent,
                                                    PhysicsMotionType.Static);
        arenaWallBodyIds[3] = Physics.CreateBoxBody(in far, in identityRotation, in endExtent,
                                                    PhysicsMotionType.Static);
    }

    private void ActivatePlayerPushBody()
    {
        if (!physicsPoolsBuilt || !PhysicsBodyIds.IsValid(playerPushBodyId))
        {
            return;
        }
        Physics.SetBodyTransform(playerPushBodyId, in player.Position, in identityRotation, true);
        Physics.SetBodyActive(playerPushBodyId, true);
        playerPushBodyActive = true;
    }

    private void DeactivatePlayerPushBody()
    {
        if (!physicsPoolsBuilt || !playerPushBodyActive || !PhysicsBodyIds.IsValid(playerPushBodyId))
        {
            playerPushBodyActive = false;
            return;
        }
        Physics.SetBodyActive(playerPushBodyId, false);
        Physics.SetBodyTransform(playerPushBodyId, in hiddenPosition, in identityRotation, true);
        playerPushBodyActive = false;
    }

    private void ActivateEnemyPushBody(ref EnemyRuntime enemy)
    {
        if (!physicsPoolsBuilt || !PhysicsBodyIds.IsValid(enemy.PushBodyId))
        {
            return;
        }
        Physics.SetBodyTransform(enemy.PushBodyId, in enemy.Position, in identityRotation, true);
        Physics.SetBodyActive(enemy.PushBodyId, true);
        enemy.PushBodyActive = true;
    }

    private void DeactivateEnemyPushBody(ref EnemyRuntime enemy)
    {
        if (!physicsPoolsBuilt || !enemy.PushBodyActive || !PhysicsBodyIds.IsValid(enemy.PushBodyId))
        {
            enemy.PushBodyActive = false;
            return;
        }
        Physics.SetBodyActive(enemy.PushBodyId, false);
        Physics.SetBodyTransform(enemy.PushBodyId, in hiddenPosition, in identityRotation, true);
        enemy.PushBodyActive = false;
    }

    private void UpdateKinematicPushBodies()
    {
        if (!physicsPoolsBuilt || state != AppState.Playing)
        {
            return;
        }
        if (playerPushBodyActive && PhysicsBodyIds.IsValid(playerPushBodyId))
        {
            Physics.MoveKinematicBody(playerPushBodyId,
                                      in player.Position,
                                      in identityRotation,
                                      KinematicMoveStepSeconds);
        }
        for (int index = 0; index < enemies.Length; ++index)
        {
            ref EnemyRuntime enemy = ref enemies[index];
            if (!enemy.Active || !enemy.PushBodyActive || !PhysicsBodyIds.IsValid(enemy.PushBodyId))
            {
                continue;
            }
            Physics.MoveKinematicBody(enemy.PushBodyId,
                                      in enemy.Position,
                                      in identityRotation,
                                      KinematicMoveStepSeconds);
        }
    }

    private void SpawnDeathDebris(Vector3 position, uint materialId, bool boss)
    {
        if (!physicsPoolsBuilt)
        {
            return;
        }
        int count = boss ? 24 : 8;
        for (int index = 0; index < count; ++index)
        {
            DebrisKind kind = boss && index % 3 == 0
                                   ? DebrisKind.BossChunk
                                   : (index % 3 == 0 ? DebrisKind.Chunk : DebrisKind.Tiny);
            SpawnDebrisPiece(kind, position, materialId, boss);
        }
    }

    private void SpawnDebrisPiece(DebrisKind kind, Vector3 origin, uint materialId, bool boss)
    {
        int slot = kind switch
        {
            DebrisKind.Tiny => AcquireDebrisSlot(0, TinyDebrisCount, ref tinyDebrisCursor),
            DebrisKind.Chunk => AcquireDebrisSlot(TinyDebrisCount, ChunkDebrisCount, ref chunkDebrisCursor),
            _ => AcquireDebrisSlot(TinyDebrisCount + ChunkDebrisCount,
                                   BossChunkDebrisCount,
                                   ref bossDebrisCursor),
        };
        if (slot < 0)
        {
            return;
        }

        ref DebrisRuntime piece = ref debris[slot];
        Vector3 spawnPosition = new(origin.X + rng.Range(-0.12f, 0.12f),
                                    Math.Max(0.25f, origin.Y + rng.Range(0.15f, 0.55f)),
                                    origin.Z + rng.Range(-0.12f, 0.12f));
        Vector3 direction = new(rng.Range(-1.0f, 1.0f),
                                rng.Range(0.35f, 0.95f),
                                rng.Range(-1.0f, 1.0f));
        float directionLength = MathF.Sqrt(direction.X * direction.X + direction.Y * direction.Y +
                                           direction.Z * direction.Z);
        if (directionLength > 0.001f)
        {
            direction = new Vector3(direction.X / directionLength,
                                    direction.Y / directionLength,
                                    direction.Z / directionLength);
        }
        float speed = boss ? rng.Range(5.0f, 9.0f) : rng.Range(3.0f, 6.5f);
        Vector3 linearVelocity = BrotatoMath.Multiply(direction, speed);
        Vector3 angularVelocity = new(rng.Range(-14.0f, 14.0f),
                                      rng.Range(-14.0f, 14.0f),
                                      rng.Range(-14.0f, 14.0f));
        Vector4 rotation = RandomRotation();

        piece.LifetimeMs = boss ? rng.Range(1400.0f, 2600.0f) : rng.Range(750.0f, 1700.0f);
        piece.Active = true;
        Scene.SetNodePrimaryMaterial(piece.NodeId, materialId);
        Scene.SetNodeVisible(piece.NodeId, true);
        Physics.SetBodyTransform(piece.BodyId, in spawnPosition, in rotation, true);
        Physics.SetBodyVelocity(piece.BodyId, in linearVelocity, in angularVelocity);
        Physics.SetBodyActive(piece.BodyId, true);
    }

    private int AcquireDebrisSlot(int start, int count, ref int cursor)
    {
        for (int offset = 0; offset < count; ++offset)
        {
            int localIndex = (cursor + offset) % count;
            int index = start + localIndex;
            if (debris[index].Active || !PhysicsBodyIds.IsValid(debris[index].BodyId))
            {
                continue;
            }
            cursor = (localIndex + 1) % count;
            return index;
        }

        // The pool is intentionally fixed-size. Reusing its oldest round-robin slot keeps a death
        // burst visible without allocating or growing the native physics world mid-wave.
        int reused = start + cursor;
        cursor = (cursor + 1) % count;
        if (PhysicsBodyIds.IsValid(debris[reused].BodyId))
        {
            DeactivateDebris(ref debris[reused]);
            return reused;
        }
        return -1;
    }

    private Vector4 RandomRotation()
    {
        Vector3 axis = new(rng.Range(-1.0f, 1.0f),
                           rng.Range(-1.0f, 1.0f),
                           rng.Range(-1.0f, 1.0f));
        float length = MathF.Sqrt(axis.X * axis.X + axis.Y * axis.Y + axis.Z * axis.Z);
        if (length <= 0.001f)
        {
            axis = Vector3.Up;
            length = 1.0f;
        }
        float halfAngle = rng.Range(0.0f, MathF.PI * 2.0f) * 0.5f;
        float sine = MathF.Sin(halfAngle) / length;
        return new Vector4(axis.X * sine, axis.Y * sine, axis.Z * sine, MathF.Cos(halfAngle));
    }

    private void UpdateDebris(float deltaMs)
    {
        if (!physicsPoolsBuilt)
        {
            return;
        }
        for (int index = 0; index < debris.Length; ++index)
        {
            ref DebrisRuntime piece = ref debris[index];
            if (!piece.Active)
            {
                continue;
            }
            piece.LifetimeMs -= deltaMs;
            if (piece.LifetimeMs <= 0.0f)
            {
                DeactivateDebris(ref piece);
            }
        }
    }

    private void ClearAllDebris()
    {
        tinyDebrisCursor = 0;
        chunkDebrisCursor = 0;
        bossDebrisCursor = 0;
        if (!physicsPoolsBuilt)
        {
            return;
        }
        for (int index = 0; index < debris.Length; ++index)
        {
            if (debris[index].Active)
            {
                DeactivateDebris(ref debris[index]);
            }
        }
    }

    private static void DeactivateDebris(ref DebrisRuntime piece)
    {
        if (!piece.Active)
        {
            return;
        }
        piece.Active = false;
        piece.LifetimeMs = 0.0f;
        if (NodeIds.IsValid(piece.NodeId))
        {
            Scene.SetNodeVisible(piece.NodeId, false);
        }
        if (PhysicsBodyIds.IsValid(piece.BodyId))
        {
            Vector3 zeroVelocity = Vector3.Zero;
            Physics.SetBodyActive(piece.BodyId, false);
            Physics.SetBodyTransform(piece.BodyId, in hiddenPosition, in identityRotation, true);
            Physics.SetBodyVelocity(piece.BodyId, in zeroVelocity, in zeroVelocity);
        }
    }
}
