using GkNext;
using GkNext.Interop;

namespace {{Namespace}};

/// <summary>
/// {{DisplayName}}.
/// </summary>
/// <remarks>
/// One class drives the whole game — there is no MonoBehaviour and no per-object script. Hold your
/// state in fields here and advance all of it from <see cref="OnTick"/>; plain C# classes with no
/// engine base type are the way to split it up once this file grows.
///
/// Every hook is present and in the order the engine raises them, which is what this template is
/// for. See docs/AGENT_GUIDE/CSharpGameDevelopment.md for the full picture.
/// </remarks>
[GameInstance]
public sealed class {{ProjectName}}Game : NextGameInstance
{
    private const float CubeHeight = 1.0f;
    private const float SpinSpeed = 1.2f;

    /// <summary>The HUD layer. One draw list, submitted to the engine once per frame.</summary>
    private readonly ManagedImGui gui = new();

    private uint cubeNode = NodeIds.Invalid;
    private float spinAngle;
    private bool spinning = true;
    private double elapsedSeconds;

    /// <summary>Runs once, before the first frame. Load configuration and set up state here.</summary>
    protected override void OnInit()
    {
        Log.Info("[{{ProjectName}}] started");
    }

    /// <summary>
    /// Builds the scene procedurally. Only the <c>SceneBuild</c> API works here, and the live
    /// <c>Scene</c> accessors do not: nodes become addressable by id only once the scene has been
    /// committed, so a node's starting transform has to come from the spec it is built with.
    /// </summary>
    protected override void BeforeSceneRebuild()
    {
        // Models and materials are scene-wide and shared by every node that uses them. Build one
        // of each and hand out the ids; a material per node grows the material table with
        // duplicates that all say the same thing.
        uint boxModel = SceneBuild.AddBoxModel(new(-0.5f, -0.5f, -0.5f), new(0.5f, 0.5f, 0.5f));

        SceneBuild.AddRenderNode("{{ProjectName}}_Ground",
            new RenderNodeSpec(boxModel, SceneBuild.AddLambertianMaterial(new(0.34f, 0.36f, 0.40f)))
                .WithTranslation(new Vector3(0.0f, -0.5f, 0.0f))
                .WithScale(new Vector3(48.0f, 1.0f, 48.0f)));

        cubeNode = SceneBuild.AddRenderNode("{{ProjectName}}_Cube",
            new RenderNodeSpec(boxModel, SceneBuild.AddLambertianMaterial(new(0.95f, 0.55f, 0.16f)))
                .WithTranslation(new Vector3(0.0f, CubeHeight, 0.0f)));
    }

    /// <summary>The scene is committed and node ids resolve. Reach for components here.</summary>
    protected override void OnSceneLoaded()
    {
        // A procedurally built scene has no sky or sun until something asks for one.
        Sky.Apply(sunRotation: 0.6f, sunElevation: 0.9f);
    }

    /// <summary>
    /// Once per frame. Keep allocations out of here — set <c>GK_DOTNET_ALLOC_GUARD=1</c> while
    /// developing and the runtime will tell you when a tick allocates.
    /// </summary>
    protected override void OnTick(double deltaSeconds)
    {
        elapsedSeconds += deltaSeconds;

        // SceneReady comes from the base class: OnTick starts before the first scene is committed,
        // and cubeNode does not address anything until it is.
        if (!SceneReady || !spinning)
        {
            return;
        }

        spinAngle += (float)deltaSeconds * SpinSpeed;
        Scene.SetNodeRotation(cubeNode, Quat.AroundY(spinAngle));

        // Required once per tick by anything that moves a node: the scene graph is updated
        // immediately, but the renderer only re-uploads instance transforms when told to.
        Scene.MarkTransformDirty();
    }

    /// <summary>Immediate-mode HUD. Return true to tell the host the frame was consumed.</summary>
    protected override bool OnRenderUI()
    {
        gui.BeginFrame();

        gui.Panel(new UiRect(20.0f, 20.0f, 300.0f, 96.0f), 10.0f);
        gui.DrawText("{{DisplayName}}", 36.0f, 34.0f, HudPalette.Text, 1.2f);
        gui.DrawText($"time {elapsedSeconds:F1}s", 36.0f, 62.0f, HudPalette.Muted);
        gui.DrawText(spinning ? "SPACE: pause spin" : "SPACE: resume spin", 36.0f, 84.0f,
                     HudPalette.Accent);
        gui.DrawTextBottomRight("ESC quits", 18.0f, HudPalette.Muted);

        gui.EndFrame();
        return false;
    }

    /// <summary>Discrete input. Held keys are better polled from <c>Input</c> in OnTick.</summary>
    protected override bool OnInputEvent(in InputEvent inputEvent)
    {
        if (inputEvent.Type != InputEventType.KeyDown || inputEvent.IsRepeat)
        {
            return false;
        }

        switch (inputEvent.KeyCode)
        {
            case KeyCodes.Escape:
                // In the launcher or the editor this returns to the host rather than killing the
                // process; a standalone build closes.
                Engine.RequestClose();
                return true;
            case KeyCodes.Space:
                spinning = !spinning;
                return true;
            default:
                return false;
        }
    }

    /// <summary>Return true to drive the render camera this frame.</summary>
    protected override bool OnOverrideCamera(ref CameraOverride camera)
    {
        camera.Position = new Vector3(6.0f, 4.5f, 8.0f);
        camera.Target = new Vector3(0.0f, CubeHeight, 0.0f);
        camera.Up = Vector3.Up;
        camera.FieldOfView = 45.0f;
        return true;
    }

    /// <summary>Runs before the assembly is unloaded, including on every hot reload.</summary>
    protected override void OnDestroy()
    {
        Log.Info("[{{ProjectName}}] stopped");
    }
}
