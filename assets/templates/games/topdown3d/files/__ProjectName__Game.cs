using GkNext;
using GkNext.Interop;

namespace {{Namespace}};

/// <summary>
/// {{DisplayName}} — move, survive, watch the arena fill up.
/// </summary>
/// <remarks>
/// The three pieces worth keeping when you replace the gameplay: held keys are polled from
/// <c>Input</c> rather than taken from events, the enemies live in a pool that is built once and
/// recycled forever, and the camera is driven from <see cref="OnOverrideCamera"/> so it follows
/// without a node of its own.
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

    private readonly EnemySwarm enemies = new(EnemyCapacity, EnemyRadius);

    private uint playerNode = NodeIds.Invalid;
    private uint rngState = RngSeed;

    private float playerX;
    private float playerZ;
    private float health = MaxHealth;
    private float survivedSeconds;
    private float spawnTimer;
    private int score;
    private int best;
    private bool alive = true;
    private bool sceneReady;

    protected override void BeforeSceneRebuild()
    {
        Vector3 unitMin = new(-0.5f, -0.5f, -0.5f);
        Vector3 unitMax = new(0.5f, 0.5f, 0.5f);
        uint boxModel = SceneBuild.AddBoxModel(in unitMin, in unitMax);

        Vector3 groundColor = new(0.24f, 0.27f, 0.31f);
        Vector3 wallColor = new(0.16f, 0.19f, 0.23f);
        Vector3 playerColor = new(0.30f, 0.80f, 0.95f);
        Vector3 enemyColor = new(0.92f, 0.32f, 0.30f);
        uint groundMaterial = SceneBuild.AddLambertianMaterial(in groundColor);
        uint wallMaterial = SceneBuild.AddLambertianMaterial(in wallColor);
        uint playerMaterial = SceneBuild.AddLambertianMaterial(in playerColor);
        uint enemyMaterial = SceneBuild.AddLambertianMaterial(in enemyColor);

        RenderNodeSpec ground = new RenderNodeSpec(boxModel, groundMaterial)
            .WithTranslation(new Vector3(0.0f, -0.5f, 0.0f))
            .WithScale(new Vector3(ArenaHalfSize * 2.0f, 1.0f, ArenaHalfSize * 2.0f));
        SceneBuild.AddRenderNode("{{ProjectName}}_Ground", in ground);

        AddWall(boxModel, wallMaterial, 0.0f, ArenaHalfSize, ArenaHalfSize * 2.0f, 1.0f);
        AddWall(boxModel, wallMaterial, 0.0f, -ArenaHalfSize, ArenaHalfSize * 2.0f, 1.0f);
        AddWall(boxModel, wallMaterial, ArenaHalfSize, 0.0f, 1.0f, ArenaHalfSize * 2.0f);
        AddWall(boxModel, wallMaterial, -ArenaHalfSize, 0.0f, 1.0f, ArenaHalfSize * 2.0f);

        Vector3 origin = Vector3.Zero;
        uint playerModel = SceneBuild.AddSphereModel(in origin, PlayerRadius);
        RenderNodeSpec player = new RenderNodeSpec(playerModel, playerMaterial)
            .WithTranslation(new Vector3(0.0f, PlayerRadius, 0.0f));
        playerNode = SceneBuild.AddRenderNode("{{ProjectName}}_Player", in player);

        // The whole pool, built once and hidden. Nothing is created or destroyed during play.
        uint enemyModel = SceneBuild.AddSphereModel(in origin, EnemyRadius);
        Vector3 parked = new(0.0f, -20.0f, 0.0f);
        for (int i = 0; i < enemies.Capacity; i++)
        {
            RenderNodeSpec enemy = new RenderNodeSpec(enemyModel, enemyMaterial)
                .WithTranslation(parked)
                .WithVisible(false);
            enemies.SetNode(i, SceneBuild.AddRenderNode("{{ProjectName}}_Enemy", in enemy));
        }
    }

    private static void AddWall(uint model, uint material, float x, float z, float sizeX, float sizeZ)
    {
        RenderNodeSpec wall = new RenderNodeSpec(model, material)
            .WithTranslation(new Vector3(x, 0.75f, z))
            .WithScale(new Vector3(sizeX, 1.5f, sizeZ));
        SceneBuild.AddRenderNode("{{ProjectName}}_Wall", in wall);
    }

    protected override void OnSceneLoaded()
    {
        EnvironmentComponent environment = new NodeRef(Scene.GetEnvironmentNodeId()).Environment;
        if (environment.Exists)
        {
            environment.HasSky = true;
            environment.SkyIdx = 0;
            environment.SkyIntensity = 300.0f;
            environment.HasSun = true;
            environment.SunIntensity = 300.0f;
            environment.SunRotation = 0.9f;
            environment.SunElevation = 1.0f;
        }

        sceneReady = true;
        ResetRun();
    }

    protected override void OnTick(double deltaSeconds)
    {
        if (!sceneReady)
        {
            return;
        }

        float delta = MathF.Min((float)deltaSeconds, 0.1f);
        if (alive)
        {
            survivedSeconds += delta;
            UpdatePlayer(delta);
            UpdateSpawns(delta);
            enemies.Advance(delta, EnemySpeed, playerX, playerZ);
            ResolveContacts();
        }

        Vector3 playerPosition = new(playerX, PlayerRadius, playerZ);
        Scene.SetNodeTranslation(playerNode, in playerPosition);
        enemies.SyncNodes(0.0f);

        // Once per tick, for everything moved above. Without it the renderer keeps drawing the
        // transforms the nodes were built with.
        Scene.MarkTransformDirty();
    }

    private void UpdatePlayer(float deltaSeconds)
    {
        float moveX = 0.0f;
        float moveZ = 0.0f;
        if (Input.IsKeyDown("w") || Input.IsKeyDown("up"))
        {
            moveZ -= 1.0f;
        }
        if (Input.IsKeyDown("s") || Input.IsKeyDown("down"))
        {
            moveZ += 1.0f;
        }
        if (Input.IsKeyDown("a") || Input.IsKeyDown("left"))
        {
            moveX -= 1.0f;
        }
        if (Input.IsKeyDown("d") || Input.IsKeyDown("right"))
        {
            moveX += 1.0f;
        }

        // Normalised, or moving on the diagonal would be 41% faster than moving straight.
        float length = MathF.Sqrt(moveX * moveX + moveZ * moveZ);
        if (length > 0.0001f)
        {
            float step = PlayerSpeed * deltaSeconds / length;
            playerX += moveX * step;
            playerZ += moveZ * step;
        }

        float limit = ArenaHalfSize - 1.0f - PlayerRadius;
        playerX = MathF.Max(-limit, MathF.Min(limit, playerX));
        playerZ = MathF.Max(-limit, MathF.Min(limit, playerZ));
    }

    private void UpdateSpawns(float deltaSeconds)
    {
        // Interval falls from SpawnIntervalStart to SpawnIntervalFloor over SpawnRampSeconds; the
        // whole difficulty curve of this template is this one line.
        float ramp = MathF.Min(1.0f, survivedSeconds / SpawnRampSeconds);
        float interval = SpawnIntervalStart + (SpawnIntervalFloor - SpawnIntervalStart) * ramp;

        spawnTimer += deltaSeconds;
        while (spawnTimer >= interval)
        {
            spawnTimer -= interval;

            float angle = NextFloat() * MathF.Tau;
            enemies.TrySpawn(MathF.Cos(angle) * SpawnRingRadius, MathF.Sin(angle) * SpawnRingRadius);
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
        Vector2 screen = UI.GetScreenSize();

        UI.DrawRectFilled(20.0f, 20.0f, 260.0f, 92.0f, Color.FromBytes(14, 20, 28, 205), 10.0f);
        UI.DrawRect(20.0f, 20.0f, 260.0f, 92.0f, Color.FromBytes(255, 255, 255, 38), 10.0f, 1.0f);

        float healthFraction = health / MaxHealth;
        UI.DrawRectFilled(36.0f, 36.0f, 228.0f, 14.0f, Color.FromBytes(40, 48, 58), 7.0f);
        UI.DrawRectFilled(36.0f, 36.0f, 228.0f * healthFraction, 14.0f,
                          healthFraction > 0.35f ? Color.FromBytes(96, 210, 140) : Color.FromBytes(226, 96, 88),
                          7.0f);
        UI.DrawText($"survived {survivedSeconds:F1}s", 36.0f, 60.0f, Color.FromBytes(200, 216, 226));
        UI.DrawText($"cleared {score}    swarm {enemies.AliveCount}", 36.0f, 84.0f,
                    Color.FromBytes(160, 186, 200));

        if (!alive)
        {
            float panelWidth = 400.0f;
            float x = (screen.X - panelWidth) * 0.5f;
            float y = screen.Y * 0.5f - 66.0f;
            UI.DrawRectFilled(x, y, panelWidth, 132.0f, Color.FromBytes(18, 26, 36, 220), 16.0f);
            UI.DrawRect(x, y, panelWidth, 132.0f, Color.FromBytes(255, 255, 255, 46), 16.0f, 1.5f);
            DrawCentered("OVERRUN", y + 22.0f, 1.7f, Color.FromBytes(255, 132, 120), in screen);
            DrawCentered($"cleared {score}    best {best}", y + 64.0f, 1.1f, Color.White, in screen);
            DrawCentered("SPACE TO RESTART", y + 98.0f, 1.0f, Color.FromBytes(124, 230, 168), in screen);
        }

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
        rngState = RngSeed;
        playerX = 0.0f;
        playerZ = 0.0f;
        health = MaxHealth;
        survivedSeconds = 0.0f;
        spawnTimer = 0.0f;
        score = 0;
        alive = true;
    }

    /// <summary>Uniform in [0,1) from a seeded xorshift, so a run is reproducible. System.Random
    /// would make the same seed mean different things on different runtimes.</summary>
    private float NextFloat()
    {
        rngState ^= rngState << 13;
        rngState ^= rngState >> 17;
        rngState ^= rngState << 5;
        return (rngState >> 8) * (1.0f / 16777216.0f);
    }

    private static void DrawCentered(string text, float y, float scale, Color color, in Vector2 screen)
    {
        Vector2 size = UI.CalcTextSize(text, scale);
        UI.DrawText(text, (screen.X - size.X) * 0.5f, y, color, scale);
    }
}
