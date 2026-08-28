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
/// Scene.MarkTransformDirty for its characters.
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
    private const float FireInterval = 0.16f;
    private const float ReloadSeconds = 1.7f;
    private const int MagazineSize = 12;
    private const float SpawnIntervalStart = 2.4f;
    private const float SpawnIntervalFloor = 0.55f;
    private const float SpawnRampSeconds = 100.0f;
    private const float SpawnRingRadius = 22.0f;
    private const uint RngSeed = 20260828u;

    private readonly TpsCamera camera = new(LookSensitivity);
    private readonly EnemySquad enemies = new(EnemyCapacity);

    private uint playerPoolId;
    private uint enemyPoolId;
    private uint playerRig;
    private uint tracerNode = NodeIds.Invalid;

    private float playerX;
    private float playerZ;
    private float playerYaw;
    private float health = MaxHealth;
    private float survivedSeconds;
    private float spawnTimer;
    private float fireCooldown;
    private float reloadTimer;
    private float tracerTimer;
    private float actionClipTimer;
    private int ammo = MagazineSize;
    private int kills;
    private int best;
    private uint rngState = RngSeed;
    private bool alive = true;
    private bool sceneReady;
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

        Vector3 unitMin = new(-0.5f, -0.5f, -0.5f);
        Vector3 unitMax = new(0.5f, 0.5f, 0.5f);
        uint boxModel = SceneBuild.AddBoxModel(in unitMin, in unitMax);

        Vector3 groundColor = new(0.29f, 0.30f, 0.28f);
        Vector3 wallColor = new(0.20f, 0.21f, 0.22f);
        Vector3 coverColor = new(0.42f, 0.38f, 0.32f);
        Vector3 tracerColor = new(1.0f, 0.86f, 0.42f);
        uint groundMaterial = SceneBuild.AddLambertianMaterial(in groundColor);
        uint wallMaterial = SceneBuild.AddLambertianMaterial(in wallColor);
        uint coverMaterial = SceneBuild.AddLambertianMaterial(in coverColor);
        uint tracerMaterial = SceneBuild.AddDiffuseLightMaterial(in tracerColor, 4.0f);

        RenderNodeSpec ground = new RenderNodeSpec(boxModel, groundMaterial)
            .WithTranslation(new Vector3(0.0f, -0.5f, 0.0f))
            .WithScale(new Vector3(ArenaHalfSize * 2.0f, 1.0f, ArenaHalfSize * 2.0f));
        SceneBuild.AddRenderNode("{{ProjectName}}_Ground", in ground);

        AddBox(boxModel, wallMaterial, 0.0f, 1.5f, ArenaHalfSize, ArenaHalfSize * 2.0f, 3.0f, 1.0f);
        AddBox(boxModel, wallMaterial, 0.0f, 1.5f, -ArenaHalfSize, ArenaHalfSize * 2.0f, 3.0f, 1.0f);
        AddBox(boxModel, wallMaterial, ArenaHalfSize, 1.5f, 0.0f, 1.0f, 3.0f, ArenaHalfSize * 2.0f);
        AddBox(boxModel, wallMaterial, -ArenaHalfSize, 1.5f, 0.0f, 1.0f, 3.0f, ArenaHalfSize * 2.0f);

        // Cover, on a jittered grid so the arena reads as a place rather than a plane.
        rngState = RngSeed;
        for (int i = 0; i < 14; i++)
        {
            float x = NextFloat(-ArenaHalfSize + 5.0f, ArenaHalfSize - 5.0f);
            float z = NextFloat(-ArenaHalfSize + 5.0f, ArenaHalfSize - 5.0f);
            if (MathF.Abs(x) < 4.0f && MathF.Abs(z) < 4.0f)
            {
                continue;   // keep the spawn point clear
            }
            float width = NextFloat(1.6f, 3.4f);
            float height = NextFloat(1.0f, 2.6f);
            float depth = NextFloat(1.6f, 3.4f);
            AddBox(boxModel, coverMaterial, x, height * 0.5f, z, width, height, depth);
        }

        // One tracer, reused for every shot: a bullet that exists for four frames does not deserve
        // a node of its own each time it is fired.
        RenderNodeSpec tracer = new RenderNodeSpec(boxModel, tracerMaterial)
            .WithTranslation(new Vector3(0.0f, -50.0f, 0.0f))
            .WithVisible(false);
        tracerNode = SceneBuild.AddRenderNode("{{ProjectName}}_Tracer", in tracer);
    }

    private static void AddBox(uint model, uint material, float x, float y, float z,
                               float sizeX, float sizeY, float sizeZ)
    {
        RenderNodeSpec spec = new RenderNodeSpec(model, material)
            .WithTranslation(new Vector3(x, y, z))
            .WithScale(new Vector3(sizeX, sizeY, sizeZ));
        SceneBuild.AddRenderNode("{{ProjectName}}_Block", in spec);
    }

    protected override void OnSceneLoaded()
    {
        EnvironmentComponent environment = new NodeRef(Scene.GetEnvironmentNodeId()).Environment;
        if (environment.Exists)
        {
            environment.HasSky = true;
            environment.SkyIdx = 0;
            // Dimmer than the other templates on purpose: an arena of pale concrete under a 300
            // sun blows out to white, and the characters are what has to read here.
            environment.SkyIntensity = 120.0f;
            environment.HasSun = true;
            environment.SunIntensity = 160.0f;
            environment.SunRotation = 1.4f;
            environment.SunElevation = 0.8f;
        }

        // Only now do the rigs have nodes to be built from.
        Vector3 spawn = Vector3.Zero;
        Vector3 playerTint = new(0.36f, 0.52f, 0.72f);
        playerRig = Rig.Acquire(playerPoolId, in spawn, 0.0f, in playerTint);
        if (playerRig == 0)
        {
            Log.Error($"[{{ProjectName}}] could not acquire the player rig from {PlayerRig}");
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
        camera.Update(delta);
        UpdateTracer(delta);

        if (!alive)
        {
            return;
        }

        survivedSeconds += delta;
        fireCooldown = MathF.Max(0.0f, fireCooldown - delta);
        actionClipTimer = MathF.Max(0.0f, actionClipTimer - delta);
        UpdateReload(delta);

        float moveSpeed = UpdatePlayerMovement(delta);
        UpdateFiring();
        UpdateSpawns(delta);

        int contacts = enemies.Advance(delta, EnemySpeed, playerX, playerZ,
                                       PlayerRadius + EnemySquad.Radius);
        if (contacts > 0)
        {
            health -= contacts * ContactDamagePerSecond * delta;
            if (health <= 0.0f)
            {
                health = 0.0f;
                alive = false;
                best = Math.Max(best, kills);
                // No death clip is played: this rig has none. Its clip set is a shooter's — idle,
                // locomotion, aim, recoil, reload — so the body simply holds its last pose. Adding
                // one is an anim_die entry in the .scad, nothing here.
                actionClipTimer = 0.0f;
            }
        }

        UpdatePlayerPose(moveSpeed);
    }

    /// <summary>Camera-relative movement. Returns the speed actually travelled, which is what
    /// picks the locomotion clip.</summary>
    private float UpdatePlayerMovement(float deltaSeconds)
    {
        float moveForward = 0.0f;
        float moveRight = 0.0f;
        if (Input.IsKeyDown("w"))
        {
            moveForward += 1.0f;
        }
        if (Input.IsKeyDown("s"))
        {
            moveForward -= 1.0f;
        }
        if (Input.IsKeyDown("d"))
        {
            moveRight += 1.0f;
        }
        if (Input.IsKeyDown("a"))
        {
            moveRight -= 1.0f;
        }

        float length = MathF.Sqrt(moveForward * moveForward + moveRight * moveRight);
        float speed = camera.Aiming ? AimSpeed : (Input.IsKeyDown("shift") ? RunSpeed : WalkSpeed);
        float travelled = 0.0f;

        if (length > 0.0001f)
        {
            Vector3 forward = camera.FlatForward;
            Vector3 right = camera.FlatRight;
            float step = speed * deltaSeconds / length;
            playerX += (forward.X * moveForward + right.X * moveRight) * step;
            playerZ += (forward.Z * moveForward + right.Z * moveRight) * step;

            float limit = ArenaHalfSize - 1.0f - PlayerRadius;
            playerX = MathF.Max(-limit, MathF.Min(limit, playerX));
            playerZ = MathF.Max(-limit, MathF.Min(limit, playerZ));
            travelled = speed;
        }

        // Aiming pins the body to the camera; otherwise it turns towards where it is going. Both
        // are eased, or the character snaps round on every tap of a key.
        float targetYaw = playerYaw;
        if (camera.Aiming)
        {
            targetYaw = camera.Yaw;
        }
        else if (length > 0.0001f)
        {
            Vector3 forward = camera.FlatForward;
            Vector3 right = camera.FlatRight;
            targetYaw = MathF.Atan2(forward.X * moveForward + right.X * moveRight,
                                    forward.Z * moveForward + right.Z * moveRight);
        }
        playerYaw = TurnTowards(playerYaw, targetYaw, TurnRate * deltaSeconds);
        return travelled;
    }

    private void UpdateFiring()
    {
        // SDL button numbering: 1 is the left button.
        if (!Input.IsMouseButtonDown(1) || fireCooldown > 0.0f || reloadTimer > 0.0f)
        {
            return;
        }
        if (ammo <= 0)
        {
            BeginReload();
            return;
        }

        ammo--;
        fireCooldown = FireInterval;
        PlayPlayerClip("recoil_rifle", 0.04f);
        actionClipTimer = 0.22f;

        Vector3 direction = camera.Forward;
        int target = enemies.PickTarget(playerX, playerZ, direction, ShotRange, AimToleranceCos);
        Vector3 muzzle = new(playerX, 1.35f, playerZ);
        Vector3 hit = target >= 0
            ? enemies.PositionOf(target)
            : new Vector3(playerX + direction.X * ShotRange, 1.35f + direction.Y * ShotRange,
                          playerZ + direction.Z * ShotRange);

        ShowTracer(in muzzle, in hit);
        if (target >= 0 && enemies.Damage(target, ShotDamage))
        {
            kills++;
        }
    }

    private void UpdateReload(float deltaSeconds)
    {
        if (reloadTimer > 0.0f)
        {
            reloadTimer -= deltaSeconds;
            if (reloadTimer <= 0.0f)
            {
                ammo = MagazineSize;
            }
            return;
        }

        if (Input.IsKeyPressed("r") && ammo < MagazineSize)
        {
            BeginReload();
        }
    }

    private void BeginReload()
    {
        if (reloadTimer > 0.0f)
        {
            return;
        }
        reloadTimer = ReloadSeconds;
        PlayPlayerClip("reload_rifle", 0.12f);
        actionClipTimer = ReloadSeconds;
    }

    private void UpdateSpawns(float deltaSeconds)
    {
        float ramp = MathF.Min(1.0f, survivedSeconds / SpawnRampSeconds);
        float interval = SpawnIntervalStart + (SpawnIntervalFloor - SpawnIntervalStart) * ramp;

        spawnTimer += deltaSeconds;
        while (spawnTimer >= interval)
        {
            spawnTimer -= interval;
            float angle = NextFloat(0.0f, MathF.Tau);
            Vector3 tint = new(0.45f + NextFloat(-0.1f, 0.1f), 0.34f, 0.30f);
            enemies.TrySpawn(enemyPoolId, MathF.Cos(angle) * SpawnRingRadius,
                             MathF.Sin(angle) * SpawnRingRadius, EnemyHealth, in tint);
        }
    }

    /// <summary>Picks the clip from what the player is doing. Playing the clip that is already
    /// current is a no-op in the engine, so this can run every frame.</summary>
    private void UpdatePlayerPose(float moveSpeed)
    {
        Vector3 position = new(playerX, 0.0f, playerZ);
        Rig.SetTransform(playerRig, in position, playerYaw);

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

    /// <summary>Plays a clip on the player, remembering which one so the HUD can show it and so a
    /// missing clip is reported once rather than every frame.</summary>
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

    private void ShowTracer(in Vector3 from, in Vector3 to)
    {
        float dx = to.X - from.X;
        float dy = to.Y - from.Y;
        float dz = to.Z - from.Z;
        float length = MathF.Sqrt(dx * dx + dy * dy + dz * dz);
        if (length < 0.01f || !NodeIds.IsValid(tracerNode))
        {
            return;
        }

        Vector3 centre = new(from.X + dx * 0.5f, from.Y + dy * 0.5f, from.Z + dz * 0.5f);
        Scene.SetNodeTranslation(tracerNode, in centre);
        Vector3 scale = new(0.05f, 0.05f, length);
        Scene.SetNodeScale(tracerNode, in scale);
        Vector4 rotation = LookRotation(dx / length, dy / length, dz / length);
        Scene.SetNodeRotation(tracerNode, in rotation);
        Scene.SetNodeVisible(tracerNode, true);
        Scene.MarkTransformDirty();
        tracerTimer = 0.05f;
    }

    private void UpdateTracer(float deltaSeconds)
    {
        if (tracerTimer <= 0.0f)
        {
            return;
        }
        tracerTimer -= deltaSeconds;
        if (tracerTimer <= 0.0f && NodeIds.IsValid(tracerNode))
        {
            Scene.SetNodeVisible(tracerNode, false);
        }
    }

    protected override bool OnRenderUI()
    {
        Vector2 screen = UI.GetScreenSize();
        float centerX = MathF.Floor(screen.X * 0.5f);
        float centerY = MathF.Floor(screen.Y * 0.5f);

        // Crosshair, dark-then-light so it survives a bright wall behind it.
        float gap = camera.Aiming ? 5.0f : 9.0f;
        Color outline = Color.FromBytes(8, 12, 18, 180);
        Color mark = camera.Aiming ? Color.FromBytes(255, 214, 120) : Color.FromBytes(240, 244, 248);
        for (int i = 0; i < 2; i++)
        {
            float inset = i == 0 ? 1.0f : 0.0f;
            Color color = i == 0 ? outline : mark;
            float thickness = i == 0 ? 4.0f : 2.0f;
            UI.DrawRectFilled(centerX - gap - 8.0f + inset, centerY - thickness * 0.5f, 8.0f, thickness, color);
            UI.DrawRectFilled(centerX + gap - inset, centerY - thickness * 0.5f, 8.0f, thickness, color);
            UI.DrawRectFilled(centerX - thickness * 0.5f, centerY - gap - 8.0f + inset, thickness, 8.0f, color);
            UI.DrawRectFilled(centerX - thickness * 0.5f, centerY + gap - inset, thickness, 8.0f, color);
        }

        UI.DrawRectFilled(20.0f, 20.0f, 268.0f, 96.0f, Color.FromBytes(12, 17, 24, 208), 10.0f);
        UI.DrawRect(20.0f, 20.0f, 268.0f, 96.0f, Color.FromBytes(255, 255, 255, 38), 10.0f, 1.0f);

        float healthFraction = health / MaxHealth;
        UI.DrawRectFilled(36.0f, 36.0f, 236.0f, 14.0f, Color.FromBytes(38, 45, 55), 7.0f);
        UI.DrawRectFilled(36.0f, 36.0f, 236.0f * healthFraction, 14.0f,
                          healthFraction > 0.35f ? Color.FromBytes(96, 210, 140) : Color.FromBytes(226, 96, 88),
                          7.0f);
        UI.DrawText(reloadTimer > 0.0f ? "RELOADING" : $"AMMO {ammo} / {MagazineSize}", 36.0f, 60.0f,
                    ammo == 0 && reloadTimer <= 0.0f ? Color.FromBytes(255, 140, 128)
                                                     : Color.FromBytes(214, 228, 238));
        UI.DrawText($"kills {kills}    infected {enemies.AliveCount}    {survivedSeconds:F0}s", 36.0f, 84.0f,
                    Color.FromBytes(156, 182, 198));

        const string help = "WASD move   SHIFT run   RMB aim   LMB fire   R reload";
        Vector2 helpSize = UI.CalcTextSize(help);
        UI.DrawText(help, (screen.X - helpSize.X) * 0.5f + 1.0f, screen.Y - helpSize.Y - 21.0f,
                    Color.FromBytes(8, 12, 18, 170));
        UI.DrawText(help, (screen.X - helpSize.X) * 0.5f, screen.Y - helpSize.Y - 22.0f,
                    Color.FromBytes(255, 255, 255, 170));

        if (!alive)
        {
            float panelWidth = 420.0f;
            float x = (screen.X - panelWidth) * 0.5f;
            float y = screen.Y * 0.5f - 70.0f;
            UI.DrawRectFilled(x, y, panelWidth, 140.0f, Color.FromBytes(18, 26, 36, 224), 16.0f);
            UI.DrawRect(x, y, panelWidth, 140.0f, Color.FromBytes(255, 255, 255, 46), 16.0f, 1.5f);
            DrawCentered("YOU DIED", y + 24.0f, 1.8f, Color.FromBytes(255, 132, 120), in screen);
            DrawCentered($"kills {kills}    best {best}", y + 70.0f, 1.15f, Color.White, in screen);
            DrawCentered("SPACE TO TRY AGAIN", y + 104.0f, 1.0f, Color.FromBytes(124, 230, 168), in screen);
        }

        return false;
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
        playerX = 0.0f;
        playerZ = 0.0f;
        playerYaw = 0.0f;
        health = MaxHealth;
        survivedSeconds = 0.0f;
        spawnTimer = 0.0f;
        fireCooldown = 0.0f;
        reloadTimer = 0.0f;
        actionClipTimer = 0.0f;
        ammo = MagazineSize;
        kills = 0;
        alive = true;
        rngState = RngSeed * 3u + 1u;
        camera.Reset(0.0f);
        currentClip = string.Empty;
        PlayPlayerClip("stand_idle", 0.0f);

        Vector3 spawn = Vector3.Zero;
        Rig.SetTransform(playerRig, in spawn, 0.0f);
    }

    /// <summary>Shortest-way-round turn towards a target angle.</summary>
    private static float TurnTowards(float current, float target, float maxDelta)
    {
        float difference = target - current;
        while (difference > MathF.PI)
        {
            difference -= MathF.Tau;
        }
        while (difference < -MathF.PI)
        {
            difference += MathF.Tau;
        }
        if (MathF.Abs(difference) <= maxDelta)
        {
            return target;
        }
        return current + MathF.Sign(difference) * maxDelta;
    }

    /// <summary>Quaternion that turns +Z onto a unit direction. Written out because there is no
    /// vector math library on this side of the boundary — the interop structs are data only.</summary>
    private static Vector4 LookRotation(float x, float y, float z)
    {
        // Rotation from (0,0,1) to (x,y,z): axis = cross, angle = acos(dot).
        float dot = z;
        if (dot > 0.99999f)
        {
            return new Vector4(0.0f, 0.0f, 0.0f, 1.0f);
        }
        if (dot < -0.99999f)
        {
            return new Vector4(0.0f, 1.0f, 0.0f, 0.0f);   // 180° about Y
        }

        float axisX = -y;
        float axisY = x;
        const float axisZ = 0.0f;
        float axisLength = MathF.Sqrt(axisX * axisX + axisY * axisY);
        float angle = MathF.Acos(Math.Clamp(dot, -1.0f, 1.0f));
        float sin = MathF.Sin(angle * 0.5f);
        return new Vector4(axisX / axisLength * sin, axisY / axisLength * sin, axisZ,
                           MathF.Cos(angle * 0.5f));
    }

    /// <summary>Seeded xorshift, so the arena layout is the same every run.</summary>
    private float NextFloat(float min, float max)
    {
        rngState ^= rngState << 13;
        rngState ^= rngState >> 17;
        rngState ^= rngState << 5;
        return min + (rngState >> 8) * (1.0f / 16777216.0f) * (max - min);
    }

    private static void DrawCentered(string text, float y, float scale, Color color, in Vector2 screen)
    {
        Vector2 size = UI.CalcTextSize(text, scale);
        UI.DrawText(text, (screen.X - size.X) * 0.5f, y, color, scale);
    }
}
