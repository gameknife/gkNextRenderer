using GkNext;
using GkNext.Interop;

namespace {{Namespace}};

/// <summary>
/// {{DisplayName}} — walk a block field in first person.
/// </summary>
/// <remarks>
/// Nothing here moves a node: the world is built once and the camera does all the work. That makes
/// this the template to start from for a walkthrough, a greybox or a scene viewer — swap the
/// procedural blocks in <see cref="BeforeSceneRebuild"/> for <c>Engine.RequestLoadScene</c> and the
/// controls still apply.
///
/// There is no collision. Adding it means either testing the moved position against your own level
/// data, or giving the player a physics body through the <c>Physics</c> bindings.
/// </remarks>
[GameInstance]
public sealed class {{ProjectName}}Game : NextGameInstance
{
    // --- tuning. Move these into a JSON config under assets/configs/ once you start iterating ---
    private const float EyeHeight = 1.7f;
    private const float WalkSpeed = 6.0f;
    private const float SprintMultiplier = 2.4f;
    private const float LookSensitivity = 0.0042f;
    private const float FieldOfView = 65.0f;
    private const int BlocksPerAxis = 9;
    private const float BlockSpacing = 9.0f;
    private const uint LayoutSeed = 20260828u;

    private readonly LookController look = new(LookSensitivity);

    private float positionX;
    private float positionY = EyeHeight;
    private float positionZ = 24.0f;
    private uint rngState = LayoutSeed;
    private bool sceneReady;

    protected override void BeforeSceneRebuild()
    {
        Vector3 unitMin = new(-0.5f, -0.5f, -0.5f);
        Vector3 unitMax = new(0.5f, 0.5f, 0.5f);
        uint boxModel = SceneBuild.AddBoxModel(in unitMin, in unitMax);

        Vector3 groundColor = new(0.30f, 0.32f, 0.34f);
        uint groundMaterial = SceneBuild.AddLambertianMaterial(in groundColor);

        // Three materials shared by every block. Materials are scene-wide, so building one per node
        // would grow the material table by a hundred entries that all say the same thing.
        Vector3 blockColorA = new(0.78f, 0.74f, 0.66f);
        Vector3 blockColorB = new(0.52f, 0.60f, 0.66f);
        Vector3 blockColorC = new(0.72f, 0.48f, 0.38f);
        uint[] blockMaterials =
        [
            SceneBuild.AddLambertianMaterial(in blockColorA),
            SceneBuild.AddLambertianMaterial(in blockColorB),
            SceneBuild.AddLambertianMaterial(in blockColorC),
        ];

        float extent = BlocksPerAxis * BlockSpacing;
        RenderNodeSpec ground = new RenderNodeSpec(boxModel, groundMaterial)
            .WithTranslation(new Vector3(0.0f, -0.5f, 0.0f))
            .WithScale(new Vector3(extent * 1.4f, 1.0f, extent * 1.4f));
        SceneBuild.AddRenderNode("{{ProjectName}}_Ground", in ground);

        rngState = LayoutSeed;
        for (int gridX = 0; gridX < BlocksPerAxis; gridX++)
        {
            for (int gridZ = 0; gridZ < BlocksPerAxis; gridZ++)
            {
                // Jittered grid rather than pure noise: it fills the space evenly while still
                // looking placed by hand, and it never stacks two blocks on one spot.
                float x = (gridX - (BlocksPerAxis - 1) * 0.5f) * BlockSpacing + NextFloat(-2.5f, 2.5f);
                float z = (gridZ - (BlocksPerAxis - 1) * 0.5f) * BlockSpacing + NextFloat(-2.5f, 2.5f);
                float width = NextFloat(2.0f, 4.5f);
                float depth = NextFloat(2.0f, 4.5f);
                float height = NextFloat(2.5f, 11.0f);

                uint material = blockMaterials[(gridX + gridZ) % blockMaterials.Length];

                RenderNodeSpec block = new RenderNodeSpec(boxModel, material)
                    .WithTranslation(new Vector3(x, height * 0.5f, z))
                    .WithScale(new Vector3(width, height, depth));
                SceneBuild.AddRenderNode("{{ProjectName}}_Block", in block);
            }
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
            environment.SunRotation = 2.1f;
            environment.SunElevation = 0.7f;
        }

        look.Reset(0.0f, -0.12f);
        positionX = 0.0f;
        positionY = EyeHeight;
        positionZ = 24.0f;
        sceneReady = true;
    }

    protected override void OnTick(double deltaSeconds)
    {
        if (!sceneReady)
        {
            return;
        }

        look.Update();

        float moveForward = 0.0f;
        float moveRight = 0.0f;
        if (Input.IsKeyDown("w") || Input.IsKeyDown("up"))
        {
            moveForward += 1.0f;
        }
        if (Input.IsKeyDown("s") || Input.IsKeyDown("down"))
        {
            moveForward -= 1.0f;
        }
        if (Input.IsKeyDown("d") || Input.IsKeyDown("right"))
        {
            moveRight += 1.0f;
        }
        if (Input.IsKeyDown("a") || Input.IsKeyDown("left"))
        {
            moveRight -= 1.0f;
        }

        float length = MathF.Sqrt(moveForward * moveForward + moveRight * moveRight);
        if (length <= 0.0001f)
        {
            return;
        }

        float speed = WalkSpeed * (Input.IsKeyDown("shift") ? SprintMultiplier : 1.0f);
        float step = speed * (float)deltaSeconds / length;

        Vector3 forward = look.FlatForward;
        Vector3 right = look.Right;
        positionX += (forward.X * moveForward + right.X * moveRight) * step;
        positionZ += (forward.Z * moveForward + right.Z * moveRight) * step;
        positionY = EyeHeight;

        // No Scene.MarkTransformDirty() here on purpose: nothing in the scene moved. Only the
        // camera did, and the camera is not a node.
    }

    protected override bool OnRenderUI()
    {
        Vector2 screen = UI.GetScreenSize();

        // Crosshair, drawn dark-then-light: a white cross alone disappears against a pale wall,
        // which is most of this scene.
        float centerX = MathF.Floor(screen.X * 0.5f);
        float centerY = MathF.Floor(screen.Y * 0.5f);
        Color outline = Color.FromBytes(10, 14, 20, 170);
        Color crosshair = Color.FromBytes(255, 255, 255, 220);
        UI.DrawRectFilled(centerX - 8.0f, centerY - 2.0f, 16.0f, 4.0f, outline);
        UI.DrawRectFilled(centerX - 2.0f, centerY - 8.0f, 4.0f, 16.0f, outline);
        UI.DrawRectFilled(centerX - 7.0f, centerY - 1.0f, 14.0f, 2.0f, crosshair);
        UI.DrawRectFilled(centerX - 1.0f, centerY - 7.0f, 2.0f, 14.0f, crosshair);

        UI.DrawRectFilled(20.0f, 20.0f, 300.0f, 74.0f, Color.FromBytes(14, 20, 28, 200), 10.0f);
        UI.DrawRect(20.0f, 20.0f, 300.0f, 74.0f, Color.FromBytes(255, 255, 255, 36), 10.0f, 1.0f);
        UI.DrawText("WASD move   RMB drag look   SHIFT sprint", 34.0f, 34.0f,
                    Color.FromBytes(206, 224, 234));
        UI.DrawText($"x {positionX,7:F1}   z {positionZ,7:F1}   yaw {look.Yaw,6:F2}", 34.0f, 62.0f,
                    Color.FromBytes(150, 178, 194));
        return false;
    }

    protected override bool OnOverrideCamera(ref CameraOverride camera)
    {
        Vector3 forward = look.Forward;
        camera.Position = new Vector3(positionX, positionY, positionZ);
        camera.Target = new Vector3(positionX + forward.X, positionY + forward.Y, positionZ + forward.Z);
        camera.Up = Vector3.Up;
        camera.FieldOfView = FieldOfView;
        return true;
    }

    protected override bool OnInputEvent(in InputEvent inputEvent)
    {
        if (inputEvent.Type == InputEventType.KeyDown && inputEvent.KeyCode == KeyCodes.Escape)
        {
            // In the launcher and the editor this returns to the host; a standalone build closes.
            Engine.RequestClose();
            return true;
        }
        return false;
    }

    /// <summary>Seeded xorshift, so the layout is the same every run. System.Random would give a
    /// different field on a different runtime, which makes a level you liked unreproducible.</summary>
    private float NextFloat(float min, float max)
    {
        rngState ^= rngState << 13;
        rngState ^= rngState >> 17;
        rngState ^= rngState << 5;
        return min + (rngState >> 8) * (1.0f / 16777216.0f) * (max - min);
    }
}
