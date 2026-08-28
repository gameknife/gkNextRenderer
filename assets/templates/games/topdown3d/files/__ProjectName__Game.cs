using GkNext;
using GkNext.Interop;

namespace {{Namespace}};

/// <summary>
/// {{DisplayName}} — move, survive, watch the arena fill up.
/// </summary>
/// <remarks>
/// The three pieces worth keeping when you replace the gameplay: movement is polled as a
/// <see cref="MoveAxis"/> rather than assembled from key events, the enemies live in a pool that is
/// built once and recycled forever, and the camera is driven from <see cref="OnOverrideCamera"/> so
/// it follows without a node of its own.
/// </remarks>
[GameInstance]
public sealed class {{ProjectName}}Game : NextGameInstance
{
    // --- tuning. Move these into a JSON config under assets/configs/ once you start iterating ---
    private const float ArenaHalfSize = 22.0f;
    private const float PlayerRadius = 0.6f;
    private const float PlayerSpeed = 9.5f;
    private const float EnemyRadius = 0.5f;
    private const float EnemySpeed = 3.6f;
    private const int EnemyCapacity = 48;

    /// Inside the walls: there is no collision, so an enemy spawned outside would visibly walk
    /// through one on its way in.
    private const float SpawnRingRadius = 19.0f;
    private const float SpawnIntervalStart = 1.1f;
    private const float SpawnIntervalFloor = 0.22f;
    private const float SpawnRampSeconds = 90.0f;
    private const float ContactDamage = 12.0f;
    private const float MaxHealth = 100.0f;
    private const uint RngSeed = 20260828u;

    private readonly Rng rng = new(RngSeed);
    private readonly ManagedImGui gui = new();
    private readonly EnemySwarm enemies = new(EnemyCapacity, EnemyRadius);

    private uint playerNode = NodeIds.Invalid;
    private float playerX;
    private float playerZ;
    private float health = MaxHealth;
    private float survivedSeconds;
    private float spawnTimer;
    private int score;
    private int best;
    private bool alive = true;

    protected override void BeforeSceneRebuild()
    {
        uint boxModel = SceneBuild.AddBoxModel(new(-0.5f, -0.5f, -0.5f), new(0.5f, 0.5f, 0.5f));
        uint wallMaterial = SceneBuild.AddLambertianMaterial(new(0.16f, 0.19f, 0.23f));

        SceneBuild.AddRenderNode("{{ProjectName}}_Ground",
            new RenderNodeSpec(boxModel, SceneBuild.AddLambertianMaterial(new(0.24f, 0.27f, 0.31f)))
                .WithTranslation(new Vector3(0.0f, -0.5f, 0.0f))
                .WithScale(new Vector3(ArenaHalfSize * 2.0f, 1.0f, ArenaHalfSize * 2.0f)));

        AddWall(boxModel, wallMaterial, 0.0f, ArenaHalfSize, ArenaHalfSize * 2.0f, 1.0f);
        AddWall(boxModel, wallMaterial, 0.0f, -ArenaHalfSize, ArenaHalfSize * 2.0f, 1.0f);
        AddWall(boxModel, wallMaterial, ArenaHalfSize, 0.0f, 1.0f, ArenaHalfSize * 2.0f);
        AddWall(boxModel, wallMaterial, -ArenaHalfSize, 0.0f, 1.0f, ArenaHalfSize * 2.0f);

        playerNode = SceneBuild.AddRenderNode("{{ProjectName}}_Player",
            new RenderNodeSpec(SceneBuild.AddSphereModel(Vector3.Zero, PlayerRadius),
                               SceneBuild.AddLambertianMaterial(new(0.30f, 0.80f, 0.95f)))
                .WithTranslation(new Vector3(0.0f, PlayerRadius, 0.0f)));

        // The whole pool, built once and hidden. Nothing is created or destroyed during play.
        RenderNodeSpec enemy = new RenderNodeSpec(SceneBuild.AddSphereModel(Vector3.Zero, EnemyRadius),
                                                  SceneBuild.AddLambertianMaterial(new(0.92f, 0.32f, 0.30f)))
            .WithTranslation(new Vector3(0.0f, -20.0f, 0.0f))
            .WithVisible(false);
        for (int i = 0; i < enemies.Capacity; i++)
        {
            enemies.SetNode(i, SceneBuild.AddRenderNode("{{ProjectName}}_Enemy", enemy));
        }
    }

    private static void AddWall(uint model, uint material, float x, float z, float sizeX, float sizeZ)
    {
        SceneBuild.AddRenderNode("{{ProjectName}}_Wall",
            new RenderNodeSpec(model, material)
                .WithTranslation(new Vector3(x, 0.75f, z))
                .WithScale(new Vector3(sizeX, 1.5f, sizeZ)));
    }

    protected override void OnSceneLoaded()
    {
        Sky.Apply(sunRotation: 0.9f, sunElevation: 1.0f);
        ResetRun();
    }

    protected override void OnTick(double deltaSeconds)
    {
        if (!SceneReady)
        {
            return;
        }

        // Clamped: a frame that took a quarter of a second is a breakpoint or a scene load, and
        // stepping the simulation by it would teleport every enemy into the player.
        float delta = MathF.Min((float)deltaSeconds, 0.1f);
        if (alive)
        {
            survivedSeconds += delta;
            UpdatePlayer(delta);
            UpdateSpawns(delta);
            enemies.Advance(delta, EnemySpeed, playerX, playerZ);
            ResolveContacts();
        }

        Scene.SetNodeTranslation(playerNode, new Vector3(playerX, PlayerRadius, playerZ));
        enemies.SyncNodes();

        // Once per tick, for everything moved above. Without it the renderer keeps drawing the
        // transforms the nodes were built with.
        Scene.MarkTransformDirty();
    }

    private void UpdatePlayer(float deltaSeconds)
    {
        // Already normalised, so the diagonal is not 41% faster than the straight line, and the
        // gamepad stick is included without this code knowing about it.
        MoveAxis move = MoveAxis.Poll();
        float step = PlayerSpeed * deltaSeconds;
        float limit = ArenaHalfSize - 1.0f - PlayerRadius;

        // Forward is away from the camera, which looks down -Z here.
        playerX = Mathx.Clamp(playerX + move.Right * step, -limit, limit);
        playerZ = Mathx.Clamp(playerZ - move.Forward * step, -limit, limit);
    }

    private void UpdateSpawns(float deltaSeconds)
    {
        // Interval falls from SpawnIntervalStart to SpawnIntervalFloor over SpawnRampSeconds; the
        // whole difficulty curve of this template is this one line.
        float interval = Mathx.Lerp(SpawnIntervalStart, SpawnIntervalFloor,
                                    Mathx.Saturate(survivedSeconds / SpawnRampSeconds));

        spawnTimer += deltaSeconds;
        while (spawnTimer >= interval)
        {
            spawnTimer -= interval;
            Vector3 direction = rng.NextDirectionXZ();
            enemies.TrySpawn(direction.X * SpawnRingRadius, direction.Z * SpawnRingRadius);
        }
    }

    private void ResolveContacts()
    {
        int contacts = enemies.ConsumeContacts(playerX, playerZ, PlayerRadius + EnemyRadius);
        if (contacts == 0)
        {
            return;
        }

        score += contacts;
        health -= contacts * ContactDamage;
        if (health <= 0.0f)
        {
            health = 0.0f;
            alive = false;
            best = Math.Max(best, score);
        }
    }

    protected override bool OnInputEvent(in InputEvent inputEvent)
    {
        if (inputEvent.Type != InputEventType.KeyDown || inputEvent.IsRepeat)
        {
            return false;
        }

        if (inputEvent.KeyCode == KeyCodes.Escape)
        {
            // In the launcher and the editor this returns to the host; a standalone build closes.
            Engine.RequestClose();
            return true;
        }

        if (!alive && inputEvent.KeyCode == KeyCodes.Space)
        {
            ResetRun();
            return true;
        }

        return false;
    }

    protected override bool OnRenderUI()
    {
        gui.BeginFrame();

        float healthFraction = health / MaxHealth;
        gui.Panel(new UiRect(20.0f, 20.0f, 260.0f, 92.0f), 10.0f);
        gui.ProgressBar(new UiRect(36.0f, 36.0f, 228.0f, 14.0f), healthFraction,
                        HudPalette.Bar(healthFraction));
        gui.DrawText($"survived {survivedSeconds:F1}s", 36.0f, 60.0f, HudPalette.Text);
        gui.DrawText($"cleared {score}    swarm {enemies.AliveCount}", 36.0f, 84.0f, HudPalette.Muted);

        if (!alive)
        {
            float y = gui.ScreenSize.Y * 0.5f - 66.0f;
            gui.PanelCenteredX(400.0f, y, 132.0f, 16.0f);
            gui.DrawTextCenteredX("OVERRUN", y + 22.0f, HudPalette.Danger, 1.7f);
            gui.DrawTextCenteredX($"cleared {score}    best {best}", y + 64.0f, HudPalette.Text, 1.1f);
            gui.DrawTextCenteredX("SPACE TO RESTART", y + 98.0f, HudPalette.Accent);
        }

        gui.EndFrame();
        return false;
    }

    /// <summary>Follows the player from behind and above. No camera node, no parenting — the
    /// override runs every frame and simply reports where the camera should be.</summary>
    protected override bool OnOverrideCamera(ref CameraOverride camera)
    {
        camera.Position = new Vector3(playerX, 21.0f, playerZ + 15.0f);
        camera.Target = new Vector3(playerX, 0.0f, playerZ);
        camera.Up = Vector3.Up;
        camera.FieldOfView = 45.0f;
        return true;
    }

    private void ResetRun()
    {
        enemies.Reset();
        rng.Reset(RngSeed);
        playerX = 0.0f;
        playerZ = 0.0f;
        health = MaxHealth;
        survivedSeconds = 0.0f;
        spawnTimer = 0.0f;
        score = 0;
        alive = true;
    }
}
