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
/// The loop is the point of this template: input is read for the frame, the simulation advances on
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

    private readonly Rng rng = new(RngSeed);
    private readonly ManagedImGui gui = new();
    private readonly ObstacleField obstacles =
        new(ObstacleCount, ObstacleSpacing, ObstacleGapHeight, ObstacleWallWidth, WorldHalfHeight);

    private uint playerNode = NodeIds.Invalid;
    private RunState state = RunState.Ready;
    private float playerY;
    private float fixedAccumulator;
    private float deadTimer;
    private int score;
    private int best;

    protected override void BeforeSceneRebuild()
    {
        uint boxModel = SceneBuild.AddBoxModel(new(-0.5f, -0.5f, -0.5f), new(0.5f, 0.5f, 0.5f));
        uint playerMaterial = SceneBuild.AddLambertianMaterial(new(1.00f, 0.78f, 0.20f));
        uint wallMaterial = SceneBuild.AddLambertianMaterial(new(0.40f, 0.62f, 0.52f));
        uint backdropMaterial = SceneBuild.AddLambertianMaterial(new(0.10f, 0.16f, 0.24f));

        SceneBuild.AddRenderNode("{{ProjectName}}_Backdrop",
            new RenderNodeSpec(boxModel, backdropMaterial)
                .WithTranslation(new Vector3(0.0f, 0.0f, -6.0f))
                .WithScale(new Vector3(60.0f, 30.0f, 1.0f)));

        float playerSize = PlayerHalfSize * 2.0f;
        playerNode = SceneBuild.AddRenderNode("{{ProjectName}}_Player",
            new RenderNodeSpec(boxModel, playerMaterial)
                .WithTranslation(new Vector3(PlayerX, 0.0f, 0.0f))
                .WithScale(new Vector3(playerSize, playerSize, playerSize)));

        // The pool is built once, at its full size. Everything after this only moves nodes.
        RenderNodeSpec wall = new RenderNodeSpec(boxModel, wallMaterial)
            .WithTranslation(new Vector3(SpawnX, 0.0f, 0.0f))
            .WithScale(new Vector3(ObstacleWallWidth, ObstacleField.WallHalfHeight * 2.0f, 1.0f));
        for (int i = 0; i < obstacles.Count; i++)
        {
            obstacles.SetNodes(i,
                               SceneBuild.AddRenderNode("{{ProjectName}}_WallTop", wall),
                               SceneBuild.AddRenderNode("{{ProjectName}}_WallBottom", wall));
        }
    }

    protected override void OnSceneLoaded()
    {
        Sky.Apply(sunRotation: 0.0f, sunElevation: 0.55f);
        ResetRun();
    }

    protected override void OnTick(double deltaSeconds)
    {
        if (!SceneReady)
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
        float steer = MoveAxis.Poll().Forward;

        // Clamped so a long frame (a breakpoint, a scene load) cannot spiral into hundreds of steps.
        fixedAccumulator += MathF.Min(delta, 0.25f);
        while (fixedAccumulator >= FixedDeltaSeconds)
        {
            fixedAccumulator -= FixedDeltaSeconds;
            FixedStep(steer);
        }

        SyncScene();
    }

    /// <summary>
    /// One simulation step, always of exactly <see cref="FixedDeltaSeconds"/>.
    /// </summary>
    /// <remarks>
    /// Nothing in here reads the frame's delta, which is the point: the same input sequence gives
    /// the same run at 30fps and at 240fps, and the seeded gap layout makes the whole run
    /// replayable from the seed alone.
    /// </remarks>
    private void FixedStep(float steer)
    {
        if (state != RunState.Playing)
        {
            return;
        }

        float limit = WorldHalfHeight - PlayerHalfSize;
        playerY = Mathx.Clamp(playerY + steer * PlayerSpeed * FixedDeltaSeconds, -limit, limit);

        score += obstacles.Advance(FixedDeltaSeconds, ScrollSpeed, PlayerX, RecycleX, rng);

        if (obstacles.Collides(PlayerX, playerY, PlayerHalfSize))
        {
            state = RunState.Dead;
            deadTimer = 0.0f;
            best = Math.Max(best, score);
        }
    }

    /// <summary>Pushes the simulation onto the scene. Once per frame, after the last fixed step.</summary>
    private void SyncScene()
    {
        Scene.SetNodeTranslation(playerNode, new Vector3(PlayerX, playerY, 0.0f));
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
        gui.BeginFrame();
        float middle = gui.ScreenSize.Y * 0.5f;

        gui.DrawTextCenteredX($"{score}", 36.0f, HudPalette.Text, 2.2f, shadow: true);

        switch (state)
        {
            case RunState.Ready:
                gui.PanelCenteredX(420.0f, middle - 70.0f, 140.0f);
                gui.DrawTextCenteredX("{{DisplayName}}", middle - 46.0f, HudPalette.Highlight, 1.6f);
                gui.DrawTextCenteredX("W / S or the arrow keys to steer", middle - 6.0f, HudPalette.Muted);
                gui.DrawTextCenteredX("PRESS ANY KEY TO START", middle + 26.0f, HudPalette.Accent, 1.1f);
                break;

            case RunState.Dead:
                gui.PanelCenteredX(400.0f, middle - 62.0f, 128.0f);
                gui.DrawTextCenteredX("CRASHED", middle - 40.0f, HudPalette.Danger, 1.7f);
                gui.DrawTextCenteredX($"score {score}    best {best}", middle, HudPalette.Text, 1.1f);
                if (deadTimer >= DeadHitStopSeconds)
                {
                    gui.DrawTextCenteredX("PRESS ANY KEY TO RETRY", middle + 32.0f, HudPalette.Accent);
                }
                break;
        }

        gui.EndFrame();
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
        // Reseeding is what makes every run identical rather than merely reproducible in aggregate.
        rng.Reset(RngSeed);
        obstacles.Reset(SpawnX, rng);
        state = RunState.Ready;
        playerY = 0.0f;
        score = 0;
        fixedAccumulator = 0.0f;
        deadTimer = 0.0f;
    }
}
