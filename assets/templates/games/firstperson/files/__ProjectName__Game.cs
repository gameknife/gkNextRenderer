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
    private readonly ManagedImGui gui = new();

    private float positionX;
    private float positionZ = 24.0f;

    protected override void BeforeSceneRebuild()
    {
        // Seeded here rather than in a field initialiser, so a scene rebuild lays the field out
        // exactly the same way it did the first time.
        Rng rng = new(LayoutSeed);

        uint boxModel = SceneBuild.AddBoxModel(new(-0.5f, -0.5f, -0.5f), new(0.5f, 0.5f, 0.5f));

        // Three materials shared by every block. Materials are scene-wide, so building one per node
        // would grow the material table by a hundred entries that all say the same thing.
        uint[] blockMaterials =
        [
            SceneBuild.AddLambertianMaterial(new(0.78f, 0.74f, 0.66f)),
            SceneBuild.AddLambertianMaterial(new(0.52f, 0.60f, 0.66f)),
            SceneBuild.AddLambertianMaterial(new(0.72f, 0.48f, 0.38f)),
        ];

        float extent = BlocksPerAxis * BlockSpacing * 1.4f;
        SceneBuild.AddRenderNode("{{ProjectName}}_Ground",
            new RenderNodeSpec(boxModel, SceneBuild.AddLambertianMaterial(new(0.30f, 0.32f, 0.34f)))
                .WithTranslation(new Vector3(0.0f, -0.5f, 0.0f))
                .WithScale(new Vector3(extent, 1.0f, extent)));

        for (int gridX = 0; gridX < BlocksPerAxis; gridX++)
        {
            for (int gridZ = 0; gridZ < BlocksPerAxis; gridZ++)
            {
                // Jittered grid rather than pure noise: it fills the space evenly while still
                // looking placed by hand, and it never stacks two blocks on one spot.
                float x = (gridX - (BlocksPerAxis - 1) * 0.5f) * BlockSpacing + rng.NextFloat(-2.5f, 2.5f);
                float z = (gridZ - (BlocksPerAxis - 1) * 0.5f) * BlockSpacing + rng.NextFloat(-2.5f, 2.5f);
                float height = rng.NextFloat(2.5f, 11.0f);

                SceneBuild.AddRenderNode("{{ProjectName}}_Block",
                    new RenderNodeSpec(boxModel, blockMaterials[(gridX + gridZ) % blockMaterials.Length])
                        .WithTranslation(new Vector3(x, height * 0.5f, z))
                        .WithScale(new Vector3(rng.NextFloat(2.0f, 4.5f), height,
                                               rng.NextFloat(2.0f, 4.5f))));
            }
        }
    }

    protected override void OnSceneLoaded()
    {
        Sky.Apply(sunRotation: 2.1f, sunElevation: 0.7f);

        look.Reset(0.0f, -0.12f);
        positionX = 0.0f;
        positionZ = 24.0f;
    }

    protected override void OnTick(double deltaSeconds)
    {
        if (!SceneReady)
        {
            return;
        }

        look.Update();

        MoveAxis move = MoveAxis.Poll();
        if (!move.IsMoving)
        {
            return;
        }

        float speed = WalkSpeed * (Input.IsKeyDown("shift") ? SprintMultiplier : 1.0f);
        float step = speed * (float)deltaSeconds;

        // The stick is resolved against the camera basis, which is what makes "forward" mean the
        // way the player is looking rather than a fixed world axis.
        Vector3 forward = look.FlatForward;
        Vector3 right = look.Right;
        positionX += (forward.X * move.Forward + right.X * move.Right) * step;
        positionZ += (forward.Z * move.Forward + right.Z * move.Right) * step;

        // No Scene.MarkTransformDirty() here on purpose: nothing in the scene moved. Only the
        // camera did, and the camera is not a node.
    }

    protected override bool OnRenderUI()
    {
        gui.BeginFrame();

        DrawCrosshair();
        gui.Panel(new UiRect(20.0f, 20.0f, 300.0f, 74.0f), 10.0f);
        gui.DrawText("WASD move   RMB drag look   SHIFT sprint", 34.0f, 34.0f, HudPalette.Text);
        gui.DrawText($"x {positionX,7:F1}   z {positionZ,7:F1}   yaw {look.Yaw,6:F2}", 34.0f, 62.0f,
                     HudPalette.Muted);

        gui.EndFrame();
        return false;
    }

    /// <summary>
    /// A cross at the centre of the screen, drawn dark underneath and light on top.
    /// </summary>
    /// <remarks>
    /// The two passes are not decoration. A white cross alone disappears against a pale wall, and
    /// most of this scene is pale walls; the dark pass underneath is what makes it visible on any
    /// background at all.
    /// </remarks>
    private void DrawCrosshair()
    {
        float centerX = MathF.Floor(gui.ScreenSize.X * 0.5f);
        float centerY = MathF.Floor(gui.ScreenSize.Y * 0.5f);

        gui.DrawList.AddRectFilled(new UiRect(centerX - 8.0f, centerY - 2.0f, 16.0f, 4.0f), HudPalette.Shadow);
        gui.DrawList.AddRectFilled(new UiRect(centerX - 2.0f, centerY - 8.0f, 4.0f, 16.0f), HudPalette.Shadow);
        gui.DrawList.AddRectFilled(new UiRect(centerX - 7.0f, centerY - 1.0f, 14.0f, 2.0f), Color.White);
        gui.DrawList.AddRectFilled(new UiRect(centerX - 1.0f, centerY - 7.0f, 2.0f, 14.0f), Color.White);
    }

    protected override bool OnOverrideCamera(ref CameraOverride camera)
    {
        Vector3 forward = look.Forward;
        camera.Position = new Vector3(positionX, EyeHeight, positionZ);
        camera.Target = new Vector3(positionX + forward.X, EyeHeight + forward.Y, positionZ + forward.Z);
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
}
