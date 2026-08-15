using System.Text.Json;
using GkNext;
using GkNext.Interop;

namespace Flappy;

public enum GameState
{
    Ready,
    Playing,
    Dead,
}

public struct BirdConfig
{
    public Vector3 InitialPosition;
    public float Radius;
    public float Gravity;
    public float FlapVelocity;
    public float MinVelocity;
    public float MaxVelocity;
}

public struct PipeConfig
{
    public float Width;
    public float GapHeight;
    public float GapCenterMinY;
    public float GapCenterMaxY;
    public float SpawnInterval;
    public float SpawnX;
    public float DestroyX;
    public float Speed;
    public int PoolSize;
}

public struct WorldConfig
{
    public float MinX;
    public float MaxX;
    public float MinY;
    public float MaxY;
    public float GameplayZ;
    public float BackdropZ;
}

public struct CameraConfig
{
    public Vector3 Position;
    public Vector3 Target;
    public Vector3 Up;
    public float FieldOfView;
}

public struct EnvironmentConfig
{
    public int SkyIndex;
    public float SkyIntensity;
    public float SunIntensity;
    public float SunRotation;
    public float SunElevation;
}

public struct ParallaxConfig
{
    public float MountainZ;
    public float MountainSpeed;
    public float MountainSpacing;
    public int MountainCount;
    public float VegetationZ;
    public float VegetationSpeed;
    public float VegetationSpacing;
    public int VegetationCount;
    public float CloudZ;
    public float CloudSpeed;
    public float CloudSpacing;
    public int CloudCount;
}

public struct GameplayConfig
{
    public CameraConfig Camera;
    public WorldConfig World;
    public EnvironmentConfig Environment;
    public ParallaxConfig Parallax;
    public BirdConfig Bird;
    public PipeConfig Pipe;
    public float FixedDeltaSeconds;
    public float DeadHitStopSeconds;
    public uint RngSeed;
}

public struct ReplayConfig
{
    public int MaxFrames;
    public int[] FlapFrames;
}

/// <summary>
/// Loads the same JSON files FlappyCpp reads, through the engine's package filesystem.
/// </summary>
/// <remarks>
/// Parsed with <see cref="JsonDocument"/> rather than <c>JsonSerializer.Deserialize&lt;T&gt;</c>:
/// the latter is reflection-based and would need a serializer context to survive NativeAOT. The
/// documents are small and read once, so a hand-written reader costs nothing and keeps the
/// managed side free of another generator.
///
/// Defaults mirror the C++ struct initialisers so a partial config behaves identically on both
/// sides.
/// </remarks>
public static class FlappyConfigLoader
{
    private const string GameplayPath = "assets/configs/flappy/gameplay.json";
    private const string ReplayPath = "assets/configs/flappy/replay.json";

    public static GameplayConfig LoadGameplay()
    {
        GameplayConfig config = new()
        {
            Camera = new CameraConfig
            {
                Position = new Vector3(0.0f, 0.0f, 12.0f),
                Target = Vector3.Zero,
                Up = Vector3.Up,
                FieldOfView = 50.0f,
            },
            World = new WorldConfig
            {
                MinX = -10.0f, MaxX = 10.0f, MinY = -5.6f, MaxY = 5.6f,
                GameplayZ = 0.0f, BackdropZ = -40.0f,
            },
            Bird = new BirdConfig
            {
                InitialPosition = new Vector3(-3.0f, 0.0f, 0.0f),
                Radius = 0.4f, Gravity = -22.0f, FlapVelocity = 7.5f,
                MinVelocity = -10.0f, MaxVelocity = 10.0f,
            },
            Environment = new EnvironmentConfig
            {
                SkyIndex = 0, SkyIntensity = 150.0f,
                SunIntensity = 600.0f, SunRotation = 0.0f, SunElevation = 0.65f,
            },
            Parallax = new ParallaxConfig
            {
                MountainZ = -18.0f, MountainSpeed = 0.35f, MountainSpacing = 12.0f, MountainCount = 7,
                VegetationZ = -7.0f, VegetationSpeed = 1.0f, VegetationSpacing = 3.4f, VegetationCount = 12,
                CloudZ = -26.0f, CloudSpeed = 0.18f, CloudSpacing = 14.0f, CloudCount = 6,
            },
            Pipe = new PipeConfig
            {
                Width = 1.0f, GapHeight = 2.6f, GapCenterMinY = -2.5f, GapCenterMaxY = 2.5f,
                SpawnInterval = 1.6f, SpawnX = 12.0f, DestroyX = -12.0f, Speed = 3.0f, PoolSize = 12,
            },
            FixedDeltaSeconds = 1.0f / 60.0f,
            DeadHitStopSeconds = 0.5f,
            RngSeed = 0x00C0FFEEu,
        };

        byte[] contents = Assets.ReadFile(GameplayPath);
        if (contents.Length == 0)
        {
            Log.Warn($"[FlappyCSharp] {GameplayPath} not found; using built-in defaults");
            return config;
        }

        using JsonDocument document = JsonDocument.Parse(contents);
        JsonElement root = document.RootElement;

        if (root.TryGetProperty("camera", out JsonElement camera))
        {
            config.Camera.Position = ReadVector3(camera, "position", config.Camera.Position);
            config.Camera.Target = ReadVector3(camera, "target", config.Camera.Target);
            config.Camera.Up = ReadVector3(camera, "up", config.Camera.Up);
            config.Camera.FieldOfView = ReadFloat(camera, "fieldOfView", config.Camera.FieldOfView);
        }

        if (root.TryGetProperty("world", out JsonElement world))
        {
            config.World.MinX = ReadFloat(world, "minX", config.World.MinX);
            config.World.MaxX = ReadFloat(world, "maxX", config.World.MaxX);
            config.World.MinY = ReadFloat(world, "minY", config.World.MinY);
            config.World.MaxY = ReadFloat(world, "maxY", config.World.MaxY);
            config.World.GameplayZ = ReadFloat(world, "gameplayZ", config.World.GameplayZ);
            config.World.BackdropZ = ReadFloat(world, "backdropZ", config.World.BackdropZ);
        }

        if (root.TryGetProperty("environment", out JsonElement environment))
        {
            config.Environment.SkyIndex = ReadInt(environment, "skyIndex", config.Environment.SkyIndex);
            config.Environment.SkyIntensity = ReadFloat(environment, "skyIntensity", config.Environment.SkyIntensity);
            config.Environment.SunIntensity = ReadFloat(environment, "sunIntensity", config.Environment.SunIntensity);
            config.Environment.SunRotation = ReadFloat(environment, "sunRotation", config.Environment.SunRotation);
            config.Environment.SunElevation = ReadFloat(environment, "sunElevation", config.Environment.SunElevation);
        }

        if (root.TryGetProperty("parallax", out JsonElement parallax))
        {
            config.Parallax.MountainZ = ReadFloat(parallax, "mountainZ", config.Parallax.MountainZ);
            config.Parallax.MountainSpeed = ReadFloat(parallax, "mountainSpeed", config.Parallax.MountainSpeed);
            config.Parallax.MountainSpacing = ReadFloat(parallax, "mountainSpacing", config.Parallax.MountainSpacing);
            config.Parallax.MountainCount = ReadInt(parallax, "mountainCount", config.Parallax.MountainCount);
            config.Parallax.VegetationZ = ReadFloat(parallax, "vegetationZ", config.Parallax.VegetationZ);
            config.Parallax.VegetationSpeed = ReadFloat(parallax, "vegetationSpeed", config.Parallax.VegetationSpeed);
            config.Parallax.VegetationSpacing = ReadFloat(parallax, "vegetationSpacing", config.Parallax.VegetationSpacing);
            config.Parallax.VegetationCount = ReadInt(parallax, "vegetationCount", config.Parallax.VegetationCount);
            config.Parallax.CloudZ = ReadFloat(parallax, "cloudZ", config.Parallax.CloudZ);
            config.Parallax.CloudSpeed = ReadFloat(parallax, "cloudSpeed", config.Parallax.CloudSpeed);
            config.Parallax.CloudSpacing = ReadFloat(parallax, "cloudSpacing", config.Parallax.CloudSpacing);
            config.Parallax.CloudCount = ReadInt(parallax, "cloudCount", config.Parallax.CloudCount);
        }

        if (root.TryGetProperty("bird", out JsonElement bird))
        {
            config.Bird.InitialPosition = ReadVector3(bird, "initialPosition", config.Bird.InitialPosition);
            config.Bird.Radius = ReadFloat(bird, "radius", config.Bird.Radius);
            config.Bird.Gravity = ReadFloat(bird, "gravity", config.Bird.Gravity);
            config.Bird.FlapVelocity = ReadFloat(bird, "flapVelocity", config.Bird.FlapVelocity);
            config.Bird.MinVelocity = ReadFloat(bird, "minVelocity", config.Bird.MinVelocity);
            config.Bird.MaxVelocity = ReadFloat(bird, "maxVelocity", config.Bird.MaxVelocity);
        }

        if (root.TryGetProperty("pipe", out JsonElement pipe))
        {
            config.Pipe.Width = ReadFloat(pipe, "width", config.Pipe.Width);
            config.Pipe.GapHeight = ReadFloat(pipe, "gapHeight", config.Pipe.GapHeight);
            config.Pipe.GapCenterMinY = ReadFloat(pipe, "gapCenterMinY", config.Pipe.GapCenterMinY);
            config.Pipe.GapCenterMaxY = ReadFloat(pipe, "gapCenterMaxY", config.Pipe.GapCenterMaxY);
            config.Pipe.SpawnInterval = ReadFloat(pipe, "spawnInterval", config.Pipe.SpawnInterval);
            config.Pipe.SpawnX = ReadFloat(pipe, "spawnX", config.Pipe.SpawnX);
            config.Pipe.DestroyX = ReadFloat(pipe, "destroyX", config.Pipe.DestroyX);
            config.Pipe.Speed = ReadFloat(pipe, "speed", config.Pipe.Speed);
            config.Pipe.PoolSize = ReadInt(pipe, "poolSize", config.Pipe.PoolSize);
        }

        config.FixedDeltaSeconds = ReadFloat(root, "fixedDeltaSeconds", config.FixedDeltaSeconds);
        config.DeadHitStopSeconds = ReadFloat(root, "deadHitStopSeconds", config.DeadHitStopSeconds);

        if (root.TryGetProperty("determinism", out JsonElement determinism) &&
            determinism.TryGetProperty("rngSeed", out JsonElement seed) &&
            seed.TryGetUInt32(out uint seedValue))
        {
            config.RngSeed = seedValue;
        }

        return config;
    }

    public static ReplayConfig LoadReplay()
    {
        ReplayConfig config = new() { MaxFrames = 600, FlapFrames = [] };

        byte[] contents = Assets.ReadFile(ReplayPath);
        if (contents.Length == 0)
        {
            Log.Warn($"[FlappyCSharp] {ReplayPath} not found; replay will produce no input");
            return config;
        }

        using JsonDocument document = JsonDocument.Parse(contents);
        JsonElement root = document.RootElement;
        config.MaxFrames = ReadInt(root, "maxFrames", config.MaxFrames);

        if (root.TryGetProperty("flapFrames", out JsonElement frames) &&
            frames.ValueKind == JsonValueKind.Array)
        {
            int[] parsed = new int[frames.GetArrayLength()];
            int index = 0;
            foreach (JsonElement frame in frames.EnumerateArray())
            {
                parsed[index++] = frame.GetInt32();
            }
            config.FlapFrames = parsed;
        }

        return config;
    }

    private static float ReadFloat(JsonElement element, string name, float fallback)
        => element.TryGetProperty(name, out JsonElement value) && value.ValueKind == JsonValueKind.Number
            ? value.GetSingle()
            : fallback;

    private static int ReadInt(JsonElement element, string name, int fallback)
        => element.TryGetProperty(name, out JsonElement value) && value.ValueKind == JsonValueKind.Number
            ? value.GetInt32()
            : fallback;

    private static Vector3 ReadVector3(JsonElement element, string name, Vector3 fallback)
    {
        if (!element.TryGetProperty(name, out JsonElement value) || value.ValueKind != JsonValueKind.Array ||
            value.GetArrayLength() < 3)
        {
            return fallback;
        }
        return new Vector3(
            value[0].GetSingle(),
            value[1].GetSingle(),
            value[2].GetSingle());
    }
}
