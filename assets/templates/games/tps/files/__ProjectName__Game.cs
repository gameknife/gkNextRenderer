using GkNext;
using GkNext.Interop;

namespace {{Namespace}};

/// <summary>
/// {{DisplayName}} — a third-person shooter on ScadRig characters.
/// </summary>
/// <remarks>
/// The part worth reading is the rig lifecycle, because it is the engine's and not a choice:
///
///   BeforeSceneRebuild   Rig.DeclarePool — the only moment part models can be injected, so this
///                        is where the number of characters that will ever exist is decided.
///   OnSceneLoaded        Rig.Acquire — nodes exist now; the player takes its slot here.
///   OnTick               Rig.SetTransform / Rig.PlayClip — gameplay drives them.
///
/// Animation itself is the engine's job: the rig subsystem ticks every live animator and marks the
/// scene's transforms dirty. Unlike a game that moves plain render nodes, this one never calls
/// Scene.MarkTransformDirty for its characters — only the tracer, which is an ordinary node.
///
/// Controls: WASD moves relative to the camera, Shift runs, hold the right mouse button to aim and
/// steer, left mouse fires, R reloads, Space restarts after a death, Escape leaves.
/// </remarks>
[GameInstance]
public sealed class {{ProjectName}}Game : NextGameInstance
{
    // --- assets. Both rigs ship with the engine; see docs/AGENT_GUIDE/ScadRig.md ---------------
    private const string PlayerRig = "assets/scad/characters/nextdayz_survivor.scad";
    private const string EnemyRig = "assets/scad/characters/nextdayz_infected.scad";

    // --- tuning. Move these into a JSON config under assets/configs/ once you start iterating ---
    private const float ArenaHalfSize = 26.0f;
    private const float PlayerRadius = 0.45f;
    private const float WalkSpeed = 3.2f;
    private const float RunSpeed = 6.4f;
    private const float AimSpeed = 2.0f;
    private const float TurnRate = 12.0f;
    private const float LookSensitivity = 0.0045f;
    private const int EnemyCapacity = 20;
    private const float EnemySpeed = 2.6f;
    private const float EnemyHealth = 60.0f;
    private const float ContactDamagePerSecond = 14.0f;
    private const float MaxHealth = 100.0f;
    private const float ShotDamage = 34.0f;
    private const float ShotRange = 45.0f;

    /// cos(18°): how far off the aim ray a target may sit and still be hit.
    private const float AimToleranceCos = 0.951f;
    private const float MuzzleHeight = 1.35f;

    /// <summary>How long the recoil clip owns the body before locomotion takes it back.</summary>
    private const float RecoilClipSeconds = 0.22f;
    private const float SpawnIntervalStart = 2.4f;
    private const float SpawnIntervalFloor = 0.55f;
    private const float SpawnRampSeconds = 100.0f;
    private const float SpawnRingRadius = 22.0f;
    private const uint RngSeed = 20260828u;

    // --- SDL button numbering, which is what the input bindings carry ---------------------------
    private const int LeftMouseButton = 1;

    private readonly Rng rng = new(RngSeed);
    private readonly ManagedImGui gui = new();
    private readonly TpsCamera camera = new(LookSensitivity);
    private readonly EnemySquad enemies = new(EnemyCapacity);
    private readonly Rifle rifle = new(magazineSize: 12, fireInterval: 0.16f, reloadSeconds: 1.7f);

    private uint playerPoolId;
    private uint enemyPoolId;
    private uint playerRig;

    private float playerX;
    private float playerZ;
    private float playerYaw;
    private float health = MaxHealth;
    private float survivedSeconds;
    private float spawnTimer;

    /// <summary>How long a fire or reload clip still owns the body. Locomotion waits for it.</summary>
    private float actionClipTimer;

    private int kills;
    private int best;
    private bool alive = true;

    /// <summary>Last frame's reload state, so the reload clip is played on the edge, once.</summary>
    private bool wasReloading;

    private string currentClip = string.Empty;

    protected override void OnInit()
    {
        if (!Rig.IsAvailable())
        {
            // The manifest asks for ScadLoader and NextGameplay; a host without them refuses the
            // game in its menu, so reaching this means something else went wrong.
            Log.Error("[{{ProjectName}}] no rig subsystem — the characters will not appear");
        }
    }

    protected override void BeforeSceneRebuild()
    {
        // Rig pools first: everything below only adds boxes, but the pools decide how many
        // characters can ever exist, and getting a load failure logged before the arena is built
        // makes the log readable.
        playerPoolId = Rig.DeclarePool(PlayerRig, 1);
        enemyPoolId = Rig.DeclarePool(EnemyRig, EnemyCapacity);

        // A generator of its own for the layout, so the arena is identical every time the scene
        // is built no matter how many spawns the previous run drew.
        Rng layout = new(RngSeed);

        uint boxModel = SceneBuild.AddBoxModel(new(-0.5f, -0.5f, -0.5f), new(0.5f, 0.5f, 0.5f));
        uint wallMaterial = SceneBuild.AddLambertianMaterial(new(0.20f, 0.21f, 0.22f));
        uint coverMaterial = SceneBuild.AddLambertianMaterial(new(0.42f, 0.38f, 0.32f));

        SceneBuild.AddRenderNode("{{ProjectName}}_Ground",
            new RenderNodeSpec(boxModel, SceneBuild.AddLambertianMaterial(new(0.29f, 0.30f, 0.28f)))
                .WithTranslation(new Vector3(0.0f, -0.5f, 0.0f))
                .WithScale(new Vector3(ArenaHalfSize * 2.0f, 1.0f, ArenaHalfSize * 2.0f)));

        AddBox(boxModel, wallMaterial, 0.0f, 1.5f, ArenaHalfSize, ArenaHalfSize * 2.0f, 3.0f, 1.0f);
        AddBox(boxModel, wallMaterial, 0.0f, 1.5f, -ArenaHalfSize, ArenaHalfSize * 2.0f, 3.0f, 1.0f);
        AddBox(boxModel, wallMaterial, ArenaHalfSize, 1.5f, 0.0f, 1.0f, 3.0f, ArenaHalfSize * 2.0f);
        AddBox(boxModel, wallMaterial, -ArenaHalfSize, 1.5f, 0.0f, 1.0f, 3.0f, ArenaHalfSize * 2.0f);

        // Cover, scattered so the arena reads as a place rather than a plane.
        for (int i = 0; i < 14; i++)
        {
            float x = layout.NextFloat(-ArenaHalfSize + 5.0f, ArenaHalfSize - 5.0f);
            float z = layout.NextFloat(-ArenaHalfSize + 5.0f, ArenaHalfSize - 5.0f);
            float height = layout.NextFloat(1.0f, 2.6f);
            if (MathF.Abs(x) < 4.0f && MathF.Abs(z) < 4.0f)
            {
                continue;   // keep the spawn point clear
            }
            AddBox(boxModel, coverMaterial, x, height * 0.5f, z,
                   layout.NextFloat(1.6f, 3.4f), height, layout.NextFloat(1.6f, 3.4f));
        }

        // A light-emitting material, so the tracer reads as a muzzle flash rather than a grey stick.
        rifle.SetTracerNode(SceneBuild.AddRenderNode("{{ProjectName}}_Tracer",
            new RenderNodeSpec(boxModel, SceneBuild.AddDiffuseLightMaterial(new(1.0f, 0.86f, 0.42f), 4.0f))
                .WithTranslation(new Vector3(0.0f, -50.0f, 0.0f))
                .WithVisible(false)));
    }

    private static void AddBox(uint model, uint material, float x, float y, float z,
                               float sizeX, float sizeY, float sizeZ)
    {
        SceneBuild.AddRenderNode("{{ProjectName}}_Block",
            new RenderNodeSpec(model, material)
                .WithTranslation(new Vector3(x, y, z))
                .WithScale(new Vector3(sizeX, sizeY, sizeZ)));
    }

    protected override void OnSceneLoaded()
    {
        // Dimmer than the other templates on purpose: an arena of pale concrete under a 300 sun
        // blows out to white, and the characters are what has to read here.
        Sky.Apply(skyIntensity: 120.0f, sunIntensity: 160.0f, sunRotation: 1.4f, sunElevation: 0.8f);

        // Only now do the rigs have nodes to be built from.
        playerRig = Rig.Acquire(playerPoolId, Vector3.Zero, 0.0f, new Vector3(0.36f, 0.52f, 0.72f));
        if (playerRig == 0)
        {
            Log.Error($"[{{ProjectName}}] could not acquire the player rig from {PlayerRig}");
        }

        ResetRun();
    }

    protected override void OnTick(double deltaSeconds)
    {
        if (!SceneReady)
        {
            return;
        }

        float delta = MathF.Min((float)deltaSeconds, 0.1f);
        camera.Update(delta);
        rifle.Tick(delta);

        if (!alive)
        {
            return;
        }

        survivedSeconds += delta;
        actionClipTimer = MathF.Max(0.0f, actionClipTimer - delta);

        float moveSpeed = UpdatePlayerMovement(delta);
        UpdateWeapon();
        UpdateSpawns(delta);
        UpdateEnemies(delta);
        UpdatePlayerPose(moveSpeed);
    }

    /// <summary>Camera-relative movement. Returns the speed actually travelled, which is what
    /// picks the locomotion clip.</summary>
    private float UpdatePlayerMovement(float deltaSeconds)
    {
        MoveAxis move = MoveAxis.Poll(arrowKeys: false);
        float speed = camera.Aiming ? AimSpeed : (Input.IsKeyDown("shift") ? RunSpeed : WalkSpeed);
        Vector3 forward = camera.FlatForward;
        Vector3 right = camera.FlatRight;
        float travelled = 0.0f;

        if (move.IsMoving)
        {
            float step = speed * deltaSeconds;
            float limit = ArenaHalfSize - 1.0f - PlayerRadius;
            playerX = Mathx.Clamp(playerX + (forward.X * move.Forward + right.X * move.Right) * step,
                                  -limit, limit);
            playerZ = Mathx.Clamp(playerZ + (forward.Z * move.Forward + right.Z * move.Right) * step,
                                  -limit, limit);
            travelled = speed;
        }

        // Aiming pins the body to the camera; otherwise it turns towards where it is going. Both
        // are eased the short way round, or the character snaps — and worse, spins almost the whole
        // way when the target angle happens to cross ±π.
        float targetYaw = playerYaw;
        if (camera.Aiming)
        {
            targetYaw = camera.Yaw;
        }
        else if (move.IsMoving)
        {
            targetYaw = MathF.Atan2(forward.X * move.Forward + right.X * move.Right,
                                    forward.Z * move.Forward + right.Z * move.Right);
        }
        playerYaw = Mathx.TurnTowards(playerYaw, targetYaw, TurnRate * deltaSeconds);
        return travelled;
    }

    private void UpdateWeapon()
    {
        if (Input.IsKeyPressed("r"))
        {
            rifle.BeginReload();
        }

        if (Input.IsMouseButtonDown(LeftMouseButton) && rifle.TryFire())
        {
            PlayPlayerClip("recoil_rifle", 0.04f);
            actionClipTimer = RecoilClipSeconds;
            FireShot();
        }

        // The animation follows the state, on the edge rather than at the call that caused it.
        // A reload starts in two different places — the R key, and pulling the trigger on an empty
        // magazine, which the weapon turns into a reload by itself — and playing the clip at each
        // of them is how the second one silently ends up without an animation.
        bool reloading = rifle.IsReloading;
        if (reloading && !wasReloading)
        {
            PlayPlayerClip("reload_rifle", 0.12f);
            actionClipTimer = rifle.ReloadSeconds;
        }
        wasReloading = reloading;
    }

    /// <summary>
    /// One hitscan shot: whatever the player is pointing at takes the damage, and the tracer draws
    /// the line either way.
    /// </summary>
    /// <remarks>
    /// A miss still draws a tracer, out to the weapon's range. Without it a missed shot produces no
    /// feedback at all and reads as the game having dropped the input.
    /// </remarks>
    private void FireShot()
    {
        Vector3 direction = camera.Forward;
        int target = enemies.PickTarget(playerX, playerZ, direction, ShotRange, AimToleranceCos);
        Vector3 muzzle = new(playerX, MuzzleHeight, playerZ);
        Vector3 hit = target >= 0
            ? enemies.PositionOf(target)
            : new Vector3(playerX + direction.X * ShotRange,
                          MuzzleHeight + direction.Y * ShotRange,
                          playerZ + direction.Z * ShotRange);

        rifle.ShowTracer(muzzle, hit);
        if (target >= 0 && enemies.Damage(target, ShotDamage))
        {
            kills++;
        }
    }

    private void UpdateSpawns(float deltaSeconds)
    {
        float interval = Mathx.Lerp(SpawnIntervalStart, SpawnIntervalFloor,
                                    Mathx.Saturate(survivedSeconds / SpawnRampSeconds));

        spawnTimer += deltaSeconds;
        while (spawnTimer >= interval)
        {
            spawnTimer -= interval;
            Vector3 direction = rng.NextDirectionXZ();
            enemies.TrySpawn(enemyPoolId,
                             direction.X * SpawnRingRadius,
                             direction.Z * SpawnRingRadius,
                             EnemyHealth,
                             new Vector3(0.45f + rng.NextFloat(-0.1f, 0.1f), 0.34f, 0.30f));
        }
    }

    private void UpdateEnemies(float deltaSeconds)
    {
        int contacts = enemies.Advance(deltaSeconds, EnemySpeed, playerX, playerZ,
                                       PlayerRadius + EnemySquad.Radius);
        if (contacts == 0)
        {
            return;
        }

        health -= contacts * ContactDamagePerSecond * deltaSeconds;
        if (health > 0.0f)
        {
            return;
        }

        health = 0.0f;
        alive = false;
        best = Math.Max(best, kills);
        // No death clip is played: this rig has none. Its clip set is a shooter's — idle,
        // locomotion, aim, recoil, reload — so the body simply holds its last pose. Adding one is
        // an anim_die entry in the .scad, nothing here.
        actionClipTimer = 0.0f;
    }

    /// <summary>Picks the clip from what the player is doing. Playing the clip that is already
    /// current is a no-op in the engine, so this can run every frame.</summary>
    private void UpdatePlayerPose(float moveSpeed)
    {
        Rig.SetTransform(playerRig, new Vector3(playerX, 0.0f, playerZ), playerYaw);

        if (actionClipTimer > 0.0f)
        {
            return;   // a fire or reload clip owns the body until it is done
        }

        if (moveSpeed >= RunSpeed - 0.01f)
        {
            PlayPlayerClip("stand_run_f", 0.15f);
        }
        else if (moveSpeed > 0.0f)
        {
            PlayPlayerClip("stand_walk_f", 0.15f);
        }
        else
        {
            PlayPlayerClip(camera.Aiming ? "aim_rifle_center" : "stand_idle", 0.15f);
        }
    }

    /// <summary>Plays a clip on the player, remembering which one so a missing clip is reported
    /// once rather than every frame.</summary>
    private void PlayPlayerClip(string clip, float fade)
    {
        if (playerRig == 0 || clip == currentClip)
        {
            return;
        }
        if (!Rig.HasClip(playerPoolId, clip))
        {
            Log.Warn($"[{{ProjectName}}] {PlayerRig} has no clip '{clip}'");
            return;
        }
        currentClip = clip;
        Rig.PlayClip(playerRig, clip, fade);
    }

    protected override bool OnRenderUI()
    {
        gui.BeginFrame();

        DrawCrosshair();

        float healthFraction = health / MaxHealth;
        gui.Panel(new UiRect(20.0f, 20.0f, 268.0f, 96.0f), 10.0f);
        gui.ProgressBar(new UiRect(36.0f, 36.0f, 236.0f, 14.0f), healthFraction,
                        HudPalette.Bar(healthFraction));
        if (rifle.IsReloading)
        {
            // The bar is the point: "RELOADING" alone does not say how much longer, and how much
            // longer is the only thing the player needs while it is happening.
            gui.ProgressBar(new UiRect(36.0f, 58.0f, 236.0f, 16.0f), rifle.ReloadProgress,
                            HudPalette.Highlight, "RELOADING");
        }
        else
        {
            gui.DrawText($"AMMO {rifle.Ammo} / {rifle.MagazineSize}", 36.0f, 60.0f,
                         rifle.IsEmpty ? HudPalette.Danger : HudPalette.Text);
        }
        gui.DrawText($"kills {kills}    infected {enemies.AliveCount}    {survivedSeconds:F0}s",
                     36.0f, 84.0f, HudPalette.Muted);

        gui.DrawTextCenteredX("WASD move   SHIFT run   RMB aim   LMB fire   R reload",
                              gui.ScreenSize.Y - 38.0f, HudPalette.Muted, 1.0f, shadow: true);

        if (!alive)
        {
            float y = gui.ScreenSize.Y * 0.5f - 70.0f;
            gui.PanelCenteredX(420.0f, y, 140.0f, 16.0f);
            gui.DrawTextCenteredX("YOU DIED", y + 24.0f, HudPalette.Danger, 1.8f);
            gui.DrawTextCenteredX($"kills {kills}    best {best}", y + 70.0f, HudPalette.Text, 1.15f);
            gui.DrawTextCenteredX("SPACE TO TRY AGAIN", y + 104.0f, HudPalette.Accent);
        }

        gui.EndFrame();
        return false;
    }

    /// <summary>
    /// Four ticks around the centre, dark underneath and light on top.
    /// </summary>
    /// <remarks>
    /// The gap closes when aiming, which is the only feedback the player gets that the shot has
    /// tightened. The dark pass is what keeps it visible against a pale wall.
    /// </remarks>
    private void DrawCrosshair()
    {
        float centerX = MathF.Floor(gui.ScreenSize.X * 0.5f);
        float centerY = MathF.Floor(gui.ScreenSize.Y * 0.5f);
        float gap = camera.Aiming ? 5.0f : 9.0f;
        Color mark = camera.Aiming ? HudPalette.Highlight : HudPalette.Text;

        DrawCrosshairPass(centerX, centerY, gap - 1.0f, 4.0f, HudPalette.Shadow);
        DrawCrosshairPass(centerX, centerY, gap, 2.0f, mark);
    }

    private void DrawCrosshairPass(float centerX, float centerY, float gap, float thickness, Color color)
    {
        const float arm = 8.0f;
        float half = thickness * 0.5f;
        gui.DrawList.AddRectFilled(new UiRect(centerX - gap - arm, centerY - half, arm, thickness), color);
        gui.DrawList.AddRectFilled(new UiRect(centerX + gap, centerY - half, arm, thickness), color);
        gui.DrawList.AddRectFilled(new UiRect(centerX - half, centerY - gap - arm, thickness, arm), color);
        gui.DrawList.AddRectFilled(new UiRect(centerX - half, centerY + gap, thickness, arm), color);
    }

    protected override bool OnOverrideCamera(ref CameraOverride cameraOverride)
    {
        camera.Apply(ref cameraOverride, new Vector3(playerX, 0.0f, playerZ));
        return true;
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

    private void ResetRun()
    {
        enemies.Reset();
        rifle.Reset();
        camera.Reset(0.0f);
        rng.Reset(RngSeed);

        playerX = 0.0f;
        playerZ = 0.0f;
        playerYaw = 0.0f;
        health = MaxHealth;
        survivedSeconds = 0.0f;
        spawnTimer = 0.0f;
        actionClipTimer = 0.0f;
        wasReloading = false;
        kills = 0;
        alive = true;

        currentClip = string.Empty;
        PlayPlayerClip("stand_idle", 0.0f);
        Rig.SetTransform(playerRig, Vector3.Zero, 0.0f);
    }
}
