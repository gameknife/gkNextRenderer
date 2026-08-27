using System.Text;
using System.Text.Json;
using GkNext;
using GkNext.Interop;

namespace Flappy;

/// <summary>
/// The C# half of the Flappy parity pair.
/// </summary>
/// <remarks>
/// Deliberately mirrors FlappyCppGameInstance step for step. The value of this target is that a
/// divergence in the binding layer — a call that does not happen, happens twice, or happens in the
/// wrong order — shows up as a different score or death frame, which the trace comparison catches.
/// Making the C# "nicer" than the C++ would throw that away.
///
/// It is also the worked example behind docs/AGENT_GUIDE/CSharpGameDevelopment.md, so it covers
/// every hook a game actually uses rather than only the ones parity needs.
/// </remarks>
[GameInstance]
public sealed class FlappyCSharpGameInstance : NextGameInstance
{
    private const string FlapSfx = "assets/sounds/flappy_flap.wav";
    private const string ScoreSfx = "assets/sounds/flappy_score.wav";
    private const string HitSfx = "assets/sounds/flappy_hit.wav";

    private GameplayConfig config;
    private ReplayConfig replay;
    private readonly XorShift32 rng = new();
    private readonly FlappyBird bird = new();
    private readonly FlappyPipes pipes = new();
    private ParallaxLayer mountains = new(0.0f, 1.0f, 0.0f);
    private ParallaxLayer vegetation = new(0.0f, 1.0f, 0.0f);
    private ParallaxLayer clouds = new(0.0f, 1.0f, 0.0f);

    private GameState state = GameState.Ready;
    private int score;
    private float fixedAccumulator;
    private float deadTimer;
    private bool pendingFlap;
    private bool sceneReady;
    private bool replayDone;

    protected override void OnInit()
    {
        config = FlappyConfigLoader.LoadGameplay();
        replay = FlappyConfigLoader.LoadReplay();
        rng.Reset(config.RngSeed);
        pipes.Reset(in config.Pipe);
        // Layer speed and spacing are config, so the layers are created once the config is loaded.
        mountains = new ParallaxLayer(config.Parallax.MountainSpeed, config.Parallax.MountainSpacing,
                                      config.Parallax.MountainZ);
        vegetation = new ParallaxLayer(config.Parallax.VegetationSpeed, config.Parallax.VegetationSpacing,
                                       config.Parallax.VegetationZ);
        clouds = new ParallaxLayer(config.Parallax.CloudSpeed, config.Parallax.CloudSpacing,
                                   config.Parallax.CloudZ);
        ResetRuntime();
    }

    protected override void BeforeSceneRebuild()
    {
        // Mirrors FlappyCppGameInstance::BeforeSceneRebuild: same models, same materials, same
        // layout. Everything is procedural, so identical code runs under both backends and in
        // replay mode where nothing is drawn.
        Vector3 birdCenter = Vector3.Zero;
        uint birdModel = SceneBuild.AddSphereModel(in birdCenter, config.Bird.Radius);

        Vector3 boxMin = new(-0.5f, -0.5f, -0.5f);
        Vector3 boxMax = new(0.5f, 0.5f, 0.5f);
        uint boxModel = SceneBuild.AddBoxModel(in boxMin, in boxMax);

        // A near-flat box: the backdrops are quads with just enough depth to catch light.
        Vector3 flatMin = new(-0.5f, -0.5f, -0.02f);
        Vector3 flatMax = new(0.5f, 0.5f, 0.02f);
        uint flatModel = SceneBuild.AddBoxModel(in flatMin, in flatMax);

        Vector3 birdColor = new(1.0f, 0.82f, 0.12f);
        Vector3 pipeColor = new(0.52f, 0.58f, 0.62f);
        Vector3 boundaryColor = new(0.30f, 0.78f, 0.32f);
        Vector3 backgroundColor = new(0.30f, 0.62f, 0.94f);
        Vector3 mountainColor = new(0.38f, 0.42f, 0.46f);
        Vector3 vegetationColor = new(0.24f, 0.68f, 0.22f);
        Vector3 cloudColor = new(0.92f, 0.94f, 0.96f);

        uint birdMaterial = SceneBuild.AddLambertianMaterial(in birdColor);
        uint pipeMaterial = SceneBuild.AddLambertianMaterial(in pipeColor);
        uint boundaryMaterial = SceneBuild.AddLambertianMaterial(in boundaryColor);
        uint backgroundMaterial = SceneBuild.AddLambertianMaterial(in backgroundColor);
        uint mountainMaterial = SceneBuild.AddLambertianMaterial(in mountainColor);
        uint vegetationMaterial = SceneBuild.AddLambertianMaterial(in vegetationColor);
        uint cloudMaterial = SceneBuild.AddLambertianMaterial(in cloudColor);

        RenderNodeSpec birdSpec = new RenderNodeSpec(birdModel, birdMaterial)
            .WithTranslation(config.Bird.InitialPosition);
        bird.SetNode(SceneBuild.AddRenderNode("FlappyCSharp_Bird", in birdSpec));

        float worldWidth = config.World.MaxX - config.World.MinX;
        float gameplayDepth = config.Camera.Position.Z - config.World.GameplayZ;
        float backdropDepth = config.Camera.Position.Z - config.World.BackdropZ;
        float backdropScale = gameplayDepth > 0.0f ? Math.Max(1.0f, backdropDepth / gameplayDepth) : 1.0f;

        AddStaticNode("FlappyCSharp_Floor", boxModel, boundaryMaterial,
                      new Vector3(0.0f, config.World.MinY - 0.2f, config.World.GameplayZ),
                      new Vector3(worldWidth + 8.0f, 0.4f, 0.35f));
        AddStaticNode("FlappyCSharp_Ceiling", boxModel, boundaryMaterial,
                      new Vector3(0.0f, config.World.MaxY + 0.2f, config.World.GameplayZ),
                      new Vector3(worldWidth + 8.0f, 0.4f, 0.35f));
        AddStaticNode("FlappyCSharp_Background", flatModel, backgroundMaterial,
                      new Vector3(0.0f, 0.0f, config.World.BackdropZ),
                      new Vector3((worldWidth + 8.0f) * backdropScale,
                                  (config.World.MaxY - config.World.MinY + 2.0f) * backdropScale,
                                  1.0f));

        BuildMountains(flatModel, mountainMaterial);
        BuildVegetation(flatModel, vegetationMaterial);
        BuildClouds(flatModel, cloudMaterial);

        pipes.Reset(in config.Pipe);
        // Built already parked off-screen and invisible. The live Scene.* accessors cannot be used
        // here — nodes only become addressable by id once the scene is committed — so the starting
        // state has to come from the spec itself.
        Vector3 parked = new(0.0f, -40.0f, config.World.GameplayZ);
        Vector3 pipeScale = new(config.Pipe.Width, 1.0f, 0.35f);
        for (int index = 0; index < pipes.Count; index++)
        {
            RenderNodeSpec pipeSpec = new RenderNodeSpec(boxModel, pipeMaterial)
                .WithTranslation(parked)
                .WithScale(pipeScale)
                .WithVisible(false);
            uint top = SceneBuild.AddRenderNode("FlappyCSharp_PipeTop", in pipeSpec);
            uint bottom = SceneBuild.AddRenderNode("FlappyCSharp_PipeBottom", in pipeSpec);
            pipes.SetNodes(index, top, bottom);
        }
    }

    private static void AddStaticNode(string name, uint model, uint material, Vector3 translation,
                                      Vector3 scale)
    {
        RenderNodeSpec spec = new RenderNodeSpec(model, material)
            .WithTranslation(translation)
            .WithScale(scale);
        SceneBuild.AddRenderNode(name, in spec);
    }

    // The three layers differ only in their per-index size and height formulas, which come
    // straight from FlappyCpp. They are written out rather than parameterised with delegates so a
    // reader can diff them against the C++ line by line.
    private void BuildMountains(uint model, uint material)
    {
        int count = Math.Max(1, config.Parallax.MountainCount);
        mountains.Reset(count);
        for (int index = 0; index < count; index++)
        {
            float x = (index - (count - 1) * 0.5f) * config.Parallax.MountainSpacing;
            float width = 7.0f + (index % 3) * 1.0f;
            float height = 10.0f + ((index + 1) % 3) * 1.1f;
            RenderNodeSpec spec = new RenderNodeSpec(model, material)
                .WithTranslation(new Vector3(x, -10.5f, config.Parallax.MountainZ))
                .WithScale(new Vector3(width, height, 1.0f));
            mountains.SetNode(index, SceneBuild.AddRenderNode("FlappyCSharp_Mountain", in spec), x, -10.5f);
        }
    }

    private void BuildVegetation(uint model, uint material)
    {
        int count = Math.Max(1, config.Parallax.VegetationCount);
        vegetation.Reset(count);
        for (int index = 0; index < count; index++)
        {
            float x = (index - (count - 1) * 0.5f) * config.Parallax.VegetationSpacing;
            float width = 2.7f + (index % 3) * 0.35f;
            float height = 5.6f + ((index + 2) % 4) * 0.35f;
            RenderNodeSpec spec = new RenderNodeSpec(model, material)
                .WithTranslation(new Vector3(x, -8.0f, config.Parallax.VegetationZ))
                .WithScale(new Vector3(width, height, 1.0f));
            vegetation.SetNode(index, SceneBuild.AddRenderNode("FlappyCSharp_Vegetation", in spec), x, -8.0f);
        }
    }

    private void BuildClouds(uint model, uint material)
    {
        int count = Math.Max(1, config.Parallax.CloudCount);
        clouds.Reset(count);
        for (int index = 0; index < count; index++)
        {
            float x = (index - (count - 1) * 0.5f) * config.Parallax.CloudSpacing;
            float y = 5.5f + (index % 3) * 2.4f;
            float width = 7.0f + ((index + 1) % 3) * 1.2f;
            float height = 1.3f + (index % 2) * 0.35f;
            RenderNodeSpec spec = new RenderNodeSpec(model, material)
                .WithTranslation(new Vector3(x, y, config.Parallax.CloudZ))
                .WithScale(new Vector3(width, height, 1.0f));
            clouds.SetNode(index, SceneBuild.AddRenderNode("FlappyCSharp_Cloud", in spec), x, y);
        }
    }

    protected override void OnSceneLoaded()
    {
        ApplyEnvironment();
        sceneReady = true;
        ResetRuntime();
    }

    /// <summary>
    /// Mirrors FlappyCppGameInstance::OnSceneLoaded, which writes the same fields through
    /// Scene::GetEnvSettings().
    /// </summary>
    /// <remarks>
    /// Runs after the scene is committed rather than during the build: these are reflected
    /// component properties, addressed by node id, and node ids only resolve once the scene exists.
    /// A procedurally built scene has no environment node, so asking for its id creates one — the
    /// same on-demand behaviour the C++ side gets.
    /// </remarks>
    private void ApplyEnvironment()
    {
        EnvironmentComponent environment = new NodeRef(Scene.GetEnvironmentNodeId()).Environment;
        if (!environment.Exists)
        {
            Log.Warn("[FlappyCSharp] no environment node; the scene will use default lighting");
            return;
        }

        environment.HasSky = true;
        environment.SkyIdx = config.Environment.SkyIndex;
        environment.SkyIntensity = config.Environment.SkyIntensity;
        environment.HasSun = true;
        environment.SunIntensity = config.Environment.SunIntensity;
        environment.SunRotation = config.Environment.SunRotation;
        environment.SunElevation = config.Environment.SunElevation;
    }

    protected override void OnTick(double deltaSeconds)
    {
        if (Engine.IsReplayMode())
        {
            if (sceneReady && !replayDone)
            {
                RunReplayToCompletion();
            }
            return;
        }

        float delta = (float)deltaSeconds;
        if (state == GameState.Dead)
        {
            deadTimer += delta;
        }

        fixedAccumulator += Math.Min(delta, 0.25f);
        while (fixedAccumulator >= config.FixedDeltaSeconds)
        {
            fixedAccumulator -= config.FixedDeltaSeconds;
            bool flap = pendingFlap;
            pendingFlap = false;
            FixedStep(flap);
        }

        // Moving a node updates the scene graph but does not tell the renderer that the instance
        // transforms changed, so without this the bird and pipes stay where they were first built.
        // FlappyCpp does the same thing once per tick.
        Scene.MarkTransformDirty();
    }

    protected override bool OnInputEvent(in InputEvent inputEvent)
    {
        if (inputEvent.Type != InputEventType.KeyDown && inputEvent.Type != InputEventType.MouseButtonDown)
        {
            return false;
        }
        if (inputEvent.IsRepeat)
        {
            return false;
        }

        if (inputEvent.Type == InputEventType.KeyDown && inputEvent.KeyCode == KeyCodes.Escape)
        {
            Engine.RequestClose();
            return true;
        }

        if (state == GameState.Dead)
        {
            if (deadTimer >= config.DeadHitStopSeconds)
            {
                ResetRuntime();
            }
            return true;
        }

        pendingFlap = true;
        Audio.PlaySfx(FlapSfx);
        return true;
    }

    protected override bool OnRenderUI()
    {
        // Ported from FlappyCppGameInstance::OnRenderUI, panel for panel. The engine has no HUD
        // layer of its own, so this exercises the UI bindings the same way the C++ exercises ImGui.
        Vector2 screen = UI.GetScreenSize();
        float centerX = screen.X * 0.5f;
        float centerY = screen.Y * 0.5f;

        float scoreWidth = 136.0f + Math.Max(0, score / 10) * 18.0f;
        float scoreX = centerX - scoreWidth * 0.5f;
        UI.DrawRectFilled(scoreX + 3.0f, 27.0f, scoreWidth, 56.0f, Rgba(10, 24, 34, 80), 16.0f);
        UI.DrawRectFilled(scoreX, 24.0f, scoreWidth, 56.0f, Rgba(24, 40, 52, 210), 16.0f);
        UI.DrawRect(scoreX, 24.0f, scoreWidth, 56.0f, Rgba(255, 255, 255, 40), 16.0f, 1.0f);
        DrawCenteredText($"{score}", 31.0f, 2.05f, Color.White, in screen);

        if (state == GameState.Ready)
        {
            float panelWidth = Math.Max(280.0f, Math.Min(520.0f, screen.X - 48.0f));
            float panelY = centerY - 96.0f;
            DrawPanel(panelWidth, 178.0f, panelY, in screen);
            DrawCenteredText("FLAPPY", panelY + 26.0f, 2.0f, Rgb(255, 230, 102), in screen);
            DrawCenteredText("Thread the gap. Keep the rhythm.", panelY + 76.0f, 1.0f,
                             Rgb(211, 231, 236), in screen);
            DrawCenteredText("SPACE / CLICK / GAMEPAD A", panelY + 121.0f, 1.1f,
                             Rgb(124, 230, 168), in screen);
        }
        else if (state == GameState.Dead)
        {
            float panelWidth = Math.Max(280.0f, Math.Min(500.0f, screen.X - 48.0f));
            float panelY = centerY - 88.0f;
            DrawPanel(panelWidth, 166.0f, panelY, in screen);
            DrawCenteredText("GAME OVER", panelY + 24.0f, 1.75f, Rgb(255, 134, 122), in screen);
            DrawCenteredText($"Score {score}", panelY + 73.0f, 1.25f, Color.White, in screen);
            DrawCenteredText("PRESS ANY KEY TO RESTART", panelY + 116.0f, 1.0f,
                             Rgb(124, 230, 168), in screen);
        }

        return false;
    }

    private static Color Rgb(byte r, byte g, byte b) => Color.FromBytes(r, g, b);

    private static Color Rgba(byte r, byte g, byte b, byte a) => Color.FromBytes(r, g, b, a);

    private static void DrawPanel(float width, float height, float y, in Vector2 screen)
    {
        float x = screen.X * 0.5f - width * 0.5f;
        UI.DrawRectFilled(x + 6.0f, y + 8.0f, width, height, Rgba(16, 28, 38, 80), 18.0f);
        UI.DrawRectFilled(x, y, width, height, Rgba(18, 32, 43, 210), 18.0f);
        UI.DrawRect(x, y, width, height, Rgba(255, 255, 255, 46), 18.0f, 1.5f);
    }

    /// <summary>
    /// Centred, with the same drop shadow the C++ HUD draws — the sky is bright enough that plain
    /// white text would be unreadable against it.
    /// </summary>
    private static void DrawCenteredText(string text, float y, float scale, Color color, in Vector2 screen)
    {
        Vector2 size = UI.CalcTextSize(text, scale);
        float x = (screen.X - size.X) * 0.5f;
        UI.DrawText(text, x + 2.0f, y + 2.0f, Rgba(20, 28, 35, 110), scale);
        UI.DrawText(text, x, y, color, scale);
    }

    protected override bool OnOverrideCamera(ref CameraOverride camera)
    {
        camera.Position = config.Camera.Position;
        camera.Target = config.Camera.Target;
        camera.Up = config.Camera.Up;
        camera.FieldOfView = config.Camera.FieldOfView;
        return true;
    }

    private void ResetRuntime()
    {
        rng.Reset(config.RngSeed);
        pipes.Reset(in config.Pipe);
        bird.Reset(in config.Bird);
        state = GameState.Ready;
        score = 0;
        fixedAccumulator = 0.0f;
        deadTimer = 0.0f;
        pendingFlap = false;
    }

    private void FixedStep(bool flapRequested)
    {
        if (state == GameState.Ready)
        {
            if (flapRequested)
            {
                state = GameState.Playing;
                bird.Flap(in config.Bird);
            }
            else
            {
                bird.SyncVisual();
                return;
            }
        }

        if (state != GameState.Playing)
        {
            return;
        }

        if (flapRequested)
        {
            bird.Flap(in config.Bird);
        }

        bird.Update(config.FixedDeltaSeconds, in config.Bird);
        pipes.Update(config.FixedDeltaSeconds, in config.Pipe, in config.World, rng);
        UpdateParallax(config.FixedDeltaSeconds);

        int scored = pipes.ConsumeScoreEvents(config.Bird.InitialPosition.X);
        if (scored > 0)
        {
            score += scored;
            Audio.PlaySfx(ScoreSfx);
        }

        float minBirdY = config.World.MinY + config.Bird.Radius;
        float maxBirdY = config.World.MaxY - config.Bird.Radius;
        Vector3 birdPosition = bird.Position;
        if (birdPosition.Y < minBirdY || birdPosition.Y > maxBirdY ||
            pipes.CheckCollision(in birdPosition, config.Bird.Radius, in config.Pipe))
        {
            Die();
        }
    }

    /// <summary>Cosmetic only — no RNG, no collision, so it cannot move the replay trace.</summary>
    private void UpdateParallax(float deltaSeconds)
    {
        mountains.Update(deltaSeconds);
        vegetation.Update(deltaSeconds);
        clouds.Update(deltaSeconds);
    }

    private void Die()
    {
        if (state == GameState.Dead)
        {
            return;
        }
        state = GameState.Dead;
        deadTimer = 0.0f;
        Audio.PlaySfx(HitSfx);
    }

    private void RunReplayToCompletion()
    {
        replayDone = true;
        ResetRuntime();

        HashSet<int> flapFrames = [.. replay.FlapFrames];
        int frameCount = Math.Max(0, replay.MaxFrames);
        float[] birdY = new float[frameCount];
        float[] birdVelocityY = new float[frameCount];
        int[] scores = new int[frameCount];
        GameState[] states = new GameState[frameCount];
        int deathFrame = -1;

        for (int frame = 0; frame < frameCount; frame++)
        {
            FixedStep(flapFrames.Contains(frame));
            birdY[frame] = bird.Position.Y;
            birdVelocityY[frame] = bird.VelocityY;
            scores[frame] = score;
            states[frame] = state;

            if (deathFrame < 0 && state == GameState.Dead)
            {
                deathFrame = frame;
            }
        }

        WriteReplayTrace(birdY, birdVelocityY, scores, states, deathFrame);
        Engine.RequestClose();
    }

    /// <summary>
    /// Spelled out rather than <c>Enum.ToString</c>: enum name metadata is exactly the kind of
    /// thing trimming removes, and the trace would then compare numbers against the C++ side's
    /// strings.
    /// </summary>
    private static string StateName(GameState state) => state switch
    {
        GameState.Ready => "Ready",
        GameState.Playing => "Playing",
        GameState.Dead => "Dead",
        _ => "Unknown",
    };

    private void WriteReplayTrace(float[] birdY, float[] birdVelocityY, int[] scores, GameState[] states,
                                  int deathFrame)
    {
        // No engine binding writes files: managed code has the BCL, and what the engine has to
        // supply is the one thing only it knows — where the project lives (design 4.4 decision 4).
        string projectRoot = Paths.GetProjectRoot();
        if (string.IsNullOrEmpty(projectRoot))
        {
            Log.Error("[FlappyCSharp] could not resolve the project root; replay trace not written");
            return;
        }

        string outputDir = Path.Combine(projectRoot, "out");
        Directory.CreateDirectory(outputDir);
        string outputPath = Path.Combine(outputDir, "flappy_cs_trace.json");

        using var stream = new MemoryStream();
        using (var writer = new Utf8JsonWriter(stream, new JsonWriterOptions { Indented = true }))
        {
            writer.WriteStartObject();
            writer.WriteString("implementation", "FlappyCSharp");
            writer.WriteNumber("fixedDeltaSeconds", config.FixedDeltaSeconds);
            writer.WriteNumber("rngSeed", config.RngSeed);
            writer.WriteNumber("deathFrame", deathFrame);
            writer.WriteStartArray("frames");
            for (int frame = 0; frame < birdY.Length; frame++)
            {
                writer.WriteStartObject();
                writer.WriteNumber("frame", frame);
                writer.WriteNumber("birdY", birdY[frame]);
                writer.WriteNumber("birdVelocityY", birdVelocityY[frame]);
                writer.WriteNumber("score", scores[frame]);
                writer.WriteString("state", StateName(states[frame]));
                writer.WriteEndObject();
            }
            writer.WriteEndArray();
            writer.WriteEndObject();
        }

        File.WriteAllBytes(outputPath, stream.ToArray());
        Log.Info($"[FlappyCSharp] replay trace written to {outputPath}");
    }
}
