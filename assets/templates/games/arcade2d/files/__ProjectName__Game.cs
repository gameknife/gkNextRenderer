using GkNext;
using GkNext.Interop;

namespace {{Namespace}};

/// <summary>Where a run currently is. One field, checked everywhere — no state objects.</summary>
internal enum RunState
{
    Ready,
    Playing,
    Dead,
}

/// <summary>
/// {{DisplayName}} — a side-on arcade run.
/// </summary>
/// <remarks>
/// The loop is the point of this template: input is collected per frame, the simulation advances on
/// a fixed timestep, and only then are the results pushed onto the scene nodes. Keeping those three
/// apart is what makes a run reproducible from a seed and independent of frame rate.
/// </remarks>
[GameInstance]
public sealed class {{ProjectName}}Game : NextGameInstance
{
    // --- tuning. Move these into a JSON config under assets/configs/ once you start iterating ---
    private const float FixedDeltaSeconds = 1.0f / 120.0f;
    private const uint RngSeed = 20260828u;
    private const float WorldHalfHeight = 6.0f;
    private const float PlayerX = -6.0f;
    private const float PlayerHalfSize = 0.35f;
    private const float PlayerSpeed = 9.0f;
    private const float ScrollSpeed = 7.0f;
    private const float ObstacleSpacing = 7.0f;
    private const float ObstacleWallWidth = 1.0f;
    private const float ObstacleGapHeight = 3.4f;
    private const int ObstacleCount = 6;
    private const float SpawnX = 14.0f;
    private const float RecycleX = -14.0f;
    private const float DeadHitStopSeconds = 0.6f;

    private readonly XorShift32 rng = new();
    private readonly ObstacleField obstacles =
        new(ObstacleCount, ObstacleSpacing, ObstacleGapHeight, ObstacleWallWidth, WorldHalfHeight);

    private uint playerNode = NodeIds.Invalid;
    private RunState state = RunState.Ready;
    private float playerY;
    private float fixedAccumulator;
    private float deadTimer;
    private int score;
    private int best;
    private bool sceneReady;

    protected override void OnInit()
    {
        ResetRun();
    }

    protected override void BeforeSceneRebuild()
    {
        Vector3 unitMin = new(-0.5f, -0.5f, -0.5f);
        Vector3 unitMax = new(0.5f, 0.5f, 0.5f);
        uint boxModel = SceneBuild.AddBoxModel(in unitMin, in unitMax);

        Vector3 playerColor = new(1.0f, 0.78f, 0.20f);
        Vector3 wallColor = new(0.40f, 0.62f, 0.52f);
        Vector3 backdropColor = new(0.10f, 0.16f, 0.24f);
        uint playerMaterial = SceneBuild.AddLambertianMaterial(in playerColor);
        uint wallMaterial = SceneBuild.AddLambertianMaterial(in wallColor);
        uint backdropMaterial = SceneBuild.AddLambertianMaterial(in backdropColor);

        RenderNodeSpec backdrop = new RenderNodeSpec(boxModel, backdropMaterial)
            .WithTranslation(new Vector3(0.0f, 0.0f, -6.0f))
            .WithScale(new Vector3(60.0f, 30.0f, 1.0f));
        SceneBuild.AddRenderNode("{{ProjectName}}_Backdrop", in backdrop);

        RenderNodeSpec player = new RenderNodeSpec(boxModel, playerMaterial)
            .WithTranslation(new Vector3(PlayerX, 0.0f, 0.0f))
            .WithScale(new Vector3(PlayerHalfSize * 2.0f, PlayerHalfSize * 2.0f, PlayerHalfSize * 2.0f));
        playerNode = SceneBuild.AddRenderNode("{{ProjectName}}_Player", in player);

        // The pool is built once, at its full size. Everything after this only moves nodes.
        Vector3 wallScale = new(ObstacleWallWidth, ObstacleField.WallHalfHeight * 2.0f, 1.0f);
        Vector3 parked = new(SpawnX, 0.0f, 0.0f);
        for (int i = 0; i < obstacles.Count; i++)
        {
            RenderNodeSpec wall = new RenderNodeSpec(boxModel, wallMaterial)
                .WithTranslation(parked)
                .WithScale(wallScale);
            uint top = SceneBuild.AddRenderNode("{{ProjectName}}_WallTop", in wall);
            uint bottom = SceneBuild.AddRenderNode("{{ProjectName}}_WallBottom", in wall);
            obstacles.SetNodes(i, top, bottom);
        }
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
            environment.SunRotation = 0.0f;
            environment.SunElevation = 0.55f;
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

        float delta = (float)deltaSeconds;
        if (state == RunState.Dead)
        {
            deadTimer += delta;
        }

        // Polled rather than event-driven: this is a held key, and an event stream would give one
        // nudge per key repeat rather than continuous motion.
        float steer = 0.0f;
        if (Input.IsKeyDown("w") || Input.IsKeyDown("up"))
        {
            steer += 1.0f;
        }
        if (Input.IsKeyDown("s") || Input.IsKeyDown("down"))
        {
            steer -= 1.0f;
        }

        // Clamped so a long frame (a breakpoint, a scene load) cannot spiral into hundreds of steps.
        fixedAccumulator += MathF.Min(delta, 0.25f);
        while (fixedAccumulator >= FixedDeltaSeconds)
        {
            fixedAccumulator -= FixedDeltaSeconds;
            FixedStep(steer);
        }

        SyncScene();
    }

    private void FixedStep(float steer)
    {
        if (state != RunState.Playing)
        {
            return;
        }

        playerY += steer * PlayerSpeed * FixedDeltaSeconds;
        float limit = WorldHalfHeight - PlayerHalfSize;
        playerY = MathF.Max(-limit, MathF.Min(limit, playerY));

        score += obstacles.Advance(FixedDeltaSeconds, ScrollSpeed, PlayerX, RecycleX, rng);

        if (obstacles.Collides(PlayerX, playerY, PlayerHalfSize))
        {
            state = RunState.Dead;
            deadTimer = 0.0f;
            best = Math.Max(best, score);
        }
    }

    private void SyncScene()
    {
        Vector3 playerPosition = new(PlayerX, playerY, 0.0f);
        Scene.SetNodeTranslation(playerNode, in playerPosition);
        obstacles.SyncNodes();

        // Moving nodes is not enough on its own: without this the renderer keeps drawing them where
        // they were built.
        Scene.MarkTransformDirty();
    }

    protected override bool OnInputEvent(in InputEvent inputEvent)
    {
        bool pressed = inputEvent.Type == InputEventType.KeyDown ||
                       inputEvent.Type == InputEventType.MouseButtonDown ||
                       inputEvent.Type == InputEventType.GamepadButtonDown;
        if (!pressed || inputEvent.IsRepeat)
        {
            return false;
        }

        if (inputEvent.Type == InputEventType.KeyDown && inputEvent.KeyCode == KeyCodes.Escape)
        {
            // In the launcher and the editor this returns to the host; a standalone build closes.
            Engine.RequestClose();
            return true;
        }

        switch (state)
        {
            case RunState.Ready:
                state = RunState.Playing;
                return true;
            case RunState.Dead when deadTimer >= DeadHitStopSeconds:
                // The hit stop is why this is gated: the key press that killed you must not also
                // restart you.
                ResetRun();
                return true;
            default:
                return false;
        }
    }

    protected override bool OnRenderUI()
    {
        Vector2 screen = UI.GetScreenSize();
        float centerX = screen.X * 0.5f;

        DrawCentered($"{score}", 36.0f, 2.2f, Color.White, in screen);

        switch (state)
        {
            case RunState.Ready:
                DrawPanel(centerX, screen.Y * 0.5f - 70.0f, 420.0f, 140.0f);
                DrawCentered("{{DisplayName}}", screen.Y * 0.5f - 46.0f, 1.6f,
                             Color.FromBytes(255, 224, 128), in screen);
                DrawCentered("W / S or the arrow keys to steer", screen.Y * 0.5f - 6.0f, 1.0f,
                             Color.FromBytes(206, 226, 233), in screen);
                DrawCentered("PRESS ANY KEY TO START", screen.Y * 0.5f + 26.0f, 1.1f,
                             Color.FromBytes(124, 230, 168), in screen);
                break;
            case RunState.Dead:
                DrawPanel(centerX, screen.Y * 0.5f - 62.0f, 400.0f, 128.0f);
                DrawCentered("CRASHED", screen.Y * 0.5f - 40.0f, 1.7f,
                             Color.FromBytes(255, 132, 120), in screen);
                DrawCentered($"score {score}    best {best}", screen.Y * 0.5f, 1.1f, Color.White,
                             in screen);
                if (deadTimer >= DeadHitStopSeconds)
                {
                    DrawCentered("PRESS ANY KEY TO RETRY", screen.Y * 0.5f + 32.0f, 1.0f,
                                 Color.FromBytes(124, 230, 168), in screen);
                }
                break;
            case RunState.Playing:
            default:
                break;
        }

        return false;
    }

    protected override bool OnOverrideCamera(ref CameraOverride camera)
    {
        camera.Position = new Vector3(0.0f, 0.0f, 15.0f);
        camera.Target = Vector3.Zero;
        camera.Up = Vector3.Up;
        camera.FieldOfView = 50.0f;
        return true;
    }

    private void ResetRun()
    {
        rng.Reset(RngSeed);
        obstacles.Reset(SpawnX, rng);
        state = RunState.Ready;
        playerY = 0.0f;
        score = 0;
        fixedAccumulator = 0.0f;
        deadTimer = 0.0f;
    }

    private static void DrawPanel(float centerX, float y, float width, float height)
    {
        float x = centerX - width * 0.5f;
        UI.DrawRectFilled(x + 5.0f, y + 7.0f, width, height, Color.FromBytes(8, 14, 22, 90), 16.0f);
        UI.DrawRectFilled(x, y, width, height, Color.FromBytes(18, 28, 40, 214), 16.0f);
        UI.DrawRect(x, y, width, height, Color.FromBytes(255, 255, 255, 44), 16.0f, 1.5f);
    }

    private static void DrawCentered(string text, float y, float scale, Color color, in Vector2 screen)
    {
        Vector2 size = UI.CalcTextSize(text, scale);
        float x = (screen.X - size.X) * 0.5f;
        UI.DrawText(text, x + 2.0f, y + 2.0f, Color.FromBytes(10, 16, 24, 130), scale);
        UI.DrawText(text, x, y, color, scale);
    }
}
