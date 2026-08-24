using GkNext;
using GkNext.Interop;

namespace Brotato3D;

[GameInstance]
public sealed partial class Brotato3DCSharpGameInstance : NextGameInstance
{
    private const int MaxEnemies = 256;
    private const int MaxProjectiles = 512;
    private const int MaxPickups = 256;
    private const int MaxSpawnEntries = 16;
    private const int TinyDebrisCount = 800;
    private const int ChunkDebrisCount = 480;
    private const int BossChunkDebrisCount = 80;
    private const int MaxDebris = TinyDebrisCount + ChunkDebrisCount + BossChunkDebrisCount;
    private const float HiddenY = -100.0f;
    private const float PlayerRadius = 0.6f;
    private const float PlayerBaseSpeed = 5.0f;
    private const float PlayerDashSpeed = 26.0f;
    private const float PlayerDashDurationMs = 140.0f;
    private const float PlayerDashCooldownMs = 1700.0f;
    private const int KeyLeftShift = 0x400000E1;
    private const int KeyRightShift = 0x400000E5;
    private const int GamepadButtonSouth = 0;
    private const int GamepadButtonWest = 2;
    private const int GamepadButtonStart = 6;
    private const int GamepadButtonDpadLeft = 13;
    private const int GamepadButtonDpadRight = 14;

    private readonly ManagedImGui gui = new();
    private readonly EnemyRuntime[] enemies = new EnemyRuntime[MaxEnemies];
    private readonly ProjectileRuntime[] projectiles = new ProjectileRuntime[MaxProjectiles];
    private readonly PickupRuntime[] pickups = new PickupRuntime[MaxPickups];
    private readonly DebrisRuntime[] debris = new DebrisRuntime[MaxDebris];
    private readonly uint[] arenaWallBodyIds = new uint[4];
    private readonly SpawnRuntime[] spawnEntries = new SpawnRuntime[MaxSpawnEntries];
    private readonly NodeTransform[] transformBuffer = new NodeTransform[1 + MaxEnemies + MaxProjectiles + MaxPickups + 1];
    private readonly UpgradeDef?[] upgradeChoices = new UpgradeDef?[3];
    private readonly ShopItemDef?[] shopOffers = new ShopItemDef?[3];
    private readonly Dictionary<string, uint> enemyMaterialIds = new(StringComparer.Ordinal);
    private readonly Dictionary<string, uint> projectileMaterialIds = new(StringComparer.Ordinal);
    private readonly Dictionary<string, uint> characterMaterialIds = new(StringComparer.Ordinal);

    private BrotatoDatabase database = null!;
    private BrotatoRng rng = new(0xB07A703Du);
    private PlayerRuntime player;
    private AppState state = AppState.MainMenu;
    private bool configurationReady;
    private bool sceneReady;
    private bool resultVictory;
    private int selectedCharacterIndex;
    private int selectedArenaIndex;
    private int currentWaveIndex;
    private int activeSpawnEntryCount;
    private int killCount;
    private float runElapsedSeconds;
    private float waveRemainingSeconds;
    private float weaponCooldownMs;
    private float cameraShakeMs;
    private float cameraShakeStrength;
    private Vector3 cameraTarget = Vector3.Zero;
    private string equippedWeaponId = "smg";
    private uint playerNodeId = NodeIds.Invalid;
    private uint weaponNodeId = NodeIds.Invalid;
    private uint playerMaterialId;
    private uint whiteMaterialId;
    private uint xpMaterialId;
    private uint materialPickupMaterialId;
    private uint playerPushBodyId = PhysicsBodyIds.Invalid;
    private bool playerPushBodyActive;
    private bool physicsPoolsBuilt;
    private int tinyDebrisCursor;
    private int chunkDebrisCursor;
    private int bossDebrisCursor;

    private ArenaDef CurrentArena => database.Arenas[selectedArenaIndex];
    private CharacterDef CurrentCharacter => database.Characters[selectedCharacterIndex];

    protected override void OnInit()
    {
        try
        {
            database = BrotatoDatabase.Load();
            configurationReady = true;
        }
        catch (Exception exception)
        {
            Log.Error($"[Brotato3DCSharp] configuration failed: {exception.Message}");
            Engine.RequestClose();
            return;
        }

        selectedCharacterIndex = 0;
        selectedArenaIndex = 0;
        SetAppState(AppState.MainMenu);
        Engine.RequestLoadScene(CurrentArena.ScenePath);
        Log.Info("[Brotato3DCSharp] managed gameplay loaded; hot reload intentionally disabled for this stateful target");
    }

    protected override void BeforeSceneRebuild()
    {
        if (!configurationReady)
        {
            return;
        }
        sceneReady = false;
        BuildManagedScene();
    }

    protected override void OnSceneLoaded()
    {
        if (!configurationReady)
        {
            return;
        }

        sceneReady = true;
        EnvironmentComponent environment = new NodeRef(Scene.GetEnvironmentNodeId()).Environment;
        environment.HasSun = true;
        environment.SunIntensity = 650.0f;
        environment.SunElevation = 0.72f;
        environment.HasSky = true;
        environment.SkyIntensity = 32.0f;
        ResetRuntimeState(updateScene: true);
        Log.Info($"[Brotato3DCSharp] committed managed pools for {CurrentArena.Id}");
    }

    protected override void OnTick(double deltaSeconds)
    {
        if (!sceneReady || state != AppState.Playing)
        {
            return;
        }

        float delta = Math.Min((float)deltaSeconds, 0.05f);
        float deltaMs = delta * 1000.0f;
        runElapsedSeconds += delta;
        cameraShakeMs = Math.Max(0.0f, cameraShakeMs - deltaMs);

        UpdatePlayer(delta, deltaMs);
        UpdateWeapon(deltaMs);
        UpdateProjectiles(delta, deltaMs);
        UpdateEnemies(delta, deltaMs);
        UpdateKinematicPushBodies();
        UpdateDebris(deltaMs);
        UpdatePickups(delta);
        // Picking up XP may synchronously open the level-up overlay. Do not let wave timers or
        // spawning continue in the same tick after gameplay has entered a modal state.
        if (state == AppState.Playing)
        {
            UpdateWave(delta, deltaMs);
        }

        if (player.CurrentHp <= 0 && state == AppState.Playing)
        {
            EnterResult(victory: false);
        }

        // Flush the simulation result even when this tick opened a modal (level-up, shop or
        // result); otherwise the rendered world would remain one frame behind its frozen state.
        SubmitTransforms();
    }

    protected override bool OnInputEvent(in InputEvent inputEvent)
    {
        bool pressed = inputEvent.Type == InputEventType.KeyDown || inputEvent.Type == InputEventType.GamepadButtonDown;
        if (!pressed || inputEvent.IsRepeat)
        {
            return false;
        }

        if (state == AppState.CharacterSelect)
        {
            int characterDirection = inputEvent.Type switch
            {
                InputEventType.KeyDown when inputEvent.KeyCode == KeyCodes.Left => -1,
                InputEventType.KeyDown when inputEvent.KeyCode == KeyCodes.Right => 1,
                InputEventType.GamepadButtonDown when inputEvent.GamepadButton == GamepadButtonDpadLeft => -1,
                InputEventType.GamepadButtonDown when inputEvent.GamepadButton == GamepadButtonDpadRight => 1,
                _ => 0,
            };
            if (characterDirection != 0)
            {
                selectedCharacterIndex =
                    (selectedCharacterIndex + characterDirection + database.Characters.Count) % database.Characters.Count;
                PlaySfx("ui_move.wav", 0.42f, 35);
                return true;
            }
        }

        bool confirmInput = (inputEvent.Type == InputEventType.KeyDown && inputEvent.KeyCode == KeyCodes.Return) ||
                            (inputEvent.Type == InputEventType.GamepadButtonDown &&
                             inputEvent.GamepadButton == GamepadButtonSouth);
        if (confirmInput)
        {
            if (state == AppState.MainMenu)
            {
                SetAppState(AppState.CharacterSelect);
                PlaySfx("ui_click.wav", 0.55f, 35);
            }
            else if (state == AppState.CharacterSelect)
            {
                SelectCharacter(selectedCharacterIndex);
            }
            else if (state == AppState.Paused)
            {
                SetAppState(AppState.Playing);
            }
            else if (state == AppState.Result)
            {
                StartNewRun();
            }
            else
            {
                return false;
            }
            return true;
        }

        bool pauseInput = (inputEvent.Type == InputEventType.KeyDown && inputEvent.KeyCode == KeyCodes.Escape) ||
                          (inputEvent.Type == InputEventType.GamepadButtonDown && inputEvent.GamepadButton == GamepadButtonStart);
        if (pauseInput)
        {
            if (state == AppState.Playing)
            {
                SetAppState(AppState.Paused);
            }
            else if (state == AppState.Paused)
            {
                SetAppState(AppState.Playing);
            }
            else if (state == AppState.CharacterSelect)
            {
                SetAppState(AppState.MainMenu);
            }
            return true;
        }

        bool dashInput = (inputEvent.Type == InputEventType.KeyDown &&
                          (inputEvent.KeyCode == KeyLeftShift || inputEvent.KeyCode == KeyRightShift)) ||
                         (inputEvent.Type == InputEventType.GamepadButtonDown &&
                          inputEvent.GamepadButton == GamepadButtonWest);
        if (dashInput && state == AppState.Playing)
        {
            TryStartDash();
            return true;
        }
        return false;
    }

    protected override bool OnOverrideCamera(ref CameraOverride camera)
    {
        Vector3 target = cameraTarget;
        Vector3 position = BrotatoMath.Add(target, new Vector3(0.0f, 14.0f, 11.0f));
        if (cameraShakeMs > 0.0f)
        {
            float t = (float)Engine.GetTime() * 37.0f;
            float strength = Math.Min(0.25f, cameraShakeStrength * 0.05f);
            Vector3 jitter = new(MathF.Sin(t) * strength, 0.0f, MathF.Cos(t * 1.37f) * strength);
            position = BrotatoMath.Add(position, jitter);
            target = BrotatoMath.Add(target, BrotatoMath.Multiply(jitter, 0.5f));
        }
        camera.Position = position;
        camera.Target = target;
        camera.Up = Vector3.Up;
        camera.FieldOfView = 60.0f;
        return true;
    }

    protected override void OnDestroy()
    {
        Physics.SetWorldPaused(true);
        Audio.StopMusic();
    }

    private void StartNewRun()
    {
        if (!sceneReady)
        {
            return;
        }

        ResetRuntimeState(updateScene: true);
        CharacterDef character = CurrentCharacter;
        player.Stats = character.StartStats;
        player.MaxHp = Math.Max(1, (int)MathF.Round(player.Stats.MaxHpFlat));
        player.CurrentHp = player.MaxHp;
        equippedWeaponId = character.StartWeapon;
        playerMaterialId = characterMaterialIds[character.Id];
        Scene.SetNodePrimaryMaterial(playerNodeId, playerMaterialId);
        Scene.SetNodeVisible(playerNodeId, true);
        Scene.SetNodeVisible(weaponNodeId, true);

        currentWaveIndex = 0;
        SetAppState(AppState.Playing);
        ActivatePlayerPushBody();
        resultVictory = false;
        StartWave(currentWaveIndex);
    }

    private void GoToMainMenu()
    {
        SetAppState(AppState.MainMenu);
        ResetRuntimeState(updateScene: sceneReady);
    }

    private void EnterResult(bool victory)
    {
        resultVictory = victory;
        SetAppState(AppState.Result);
        ClearEnemies(dropLoot: false);
        ClearProjectiles();
        PlaySfx(victory ? "victory.wav" : "defeat.wav", 0.9f, 0);
    }

    private void ResetRuntimeState(bool updateScene)
    {
        rng.Reset(0xB07A703Du);
        player = new PlayerRuntime
        {
            Position = new Vector3(0.0f, PlayerRadius, 0.0f),
            Facing = new Vector3(0.0f, 0.0f, -1.0f),
            Stats = PlayerStats.Default,
            CurrentHp = 50,
            MaxHp = 50,
            Level = 1,
            DashCharges = 3,
        };
        killCount = 0;
        runElapsedSeconds = 0.0f;
        waveRemainingSeconds = 0.0f;
        weaponCooldownMs = 0.0f;
        cameraTarget = Vector3.Zero;
        cameraShakeMs = 0.0f;
        cameraShakeStrength = 0.0f;
        activeSpawnEntryCount = 0;
        Array.Clear(spawnEntries);
        Array.Clear(upgradeChoices);
        Array.Clear(shopOffers);

        for (int index = 0; index < enemies.Length; ++index)
        {
            enemies[index].Def = null;
            enemies[index].Active = false;
            DeactivateEnemyPushBody(ref enemies[index]);
        }
        for (int index = 0; index < projectiles.Length; ++index)
        {
            projectiles[index].Weapon = null;
            projectiles[index].Active = false;
        }
        for (int index = 0; index < pickups.Length; ++index)
        {
            pickups[index].Active = false;
        }
        DeactivatePlayerPushBody();
        ClearAllDebris();

        if (!updateScene)
        {
            return;
        }
        HideAllPoolNodes();
        Scene.SetNodeVisible(playerNodeId, false);
        Scene.SetNodeVisible(weaponNodeId, false);
        Scene.MarkTransformDirty();
    }

    private void ChangeArena(int direction)
    {
        selectedArenaIndex = (selectedArenaIndex + direction + database.Arenas.Count) % database.Arenas.Count;
        sceneReady = false;
        SetAppState(AppState.CharacterSelect);
        Engine.RequestLoadScene(CurrentArena.ScenePath);
    }

    private void SelectCharacter(int index)
    {
        if (index < 0 || index >= database.Characters.Count)
        {
            return;
        }
        selectedCharacterIndex = index;
        StartNewRun();
    }

    private void SetAppState(AppState nextState)
    {
        state = nextState;
        Physics.SetWorldPaused(nextState != AppState.Playing);
    }

    private void PlaySfx(string filename, float volume = 0.65f, uint minIntervalMs = 55)
        => Audio.PlaySfxEx("assets/sounds/brotato3d/sfx/" + filename, volume, minIntervalMs);

    private void StartCameraShake(float durationMs, float strength)
    {
        cameraShakeMs = Math.Max(cameraShakeMs, durationMs);
        cameraShakeStrength = Math.Max(cameraShakeStrength, strength);
    }
}
