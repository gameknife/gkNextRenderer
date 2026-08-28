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
/// See docs/AGENT_GUIDE/CSharpGameDevelopment.md for the full picture.
/// </remarks>
[GameInstance]
public sealed class {{ProjectName}}Game : NextGameInstance
{
    private const float CubeHeight = 1.0f;
    private const float SpinSpeed = 1.2f;

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
        Vector3 groundMin = new(-24.0f, -0.5f, -24.0f);
        Vector3 groundMax = new(24.0f, 0.5f, 24.0f);
        uint groundModel = SceneBuild.AddBoxModel(in groundMin, in groundMax);
        Vector3 groundColor = new(0.34f, 0.36f, 0.40f);
        uint groundMaterial = SceneBuild.AddLambertianMaterial(in groundColor);
        RenderNodeSpec groundSpec = new RenderNodeSpec(groundModel, groundMaterial)
            .WithTranslation(new Vector3(0.0f, -0.5f, 0.0f));
        SceneBuild.AddRenderNode("{{ProjectName}}_Ground", in groundSpec);

        Vector3 cubeMin = new(-0.5f, -0.5f, -0.5f);
        Vector3 cubeMax = new(0.5f, 0.5f, 0.5f);
        uint cubeModel = SceneBuild.AddBoxModel(in cubeMin, in cubeMax);
        Vector3 cubeColor = new(0.95f, 0.55f, 0.16f);
        uint cubeMaterial = SceneBuild.AddLambertianMaterial(in cubeColor);
        RenderNodeSpec cubeSpec = new RenderNodeSpec(cubeModel, cubeMaterial)
            .WithTranslation(new Vector3(0.0f, CubeHeight, 0.0f));
        cubeNode = SceneBuild.AddRenderNode("{{ProjectName}}_Cube", in cubeSpec);
    }

    /// <summary>The scene is committed and node ids resolve. Reach for components here.</summary>
    protected override void OnSceneLoaded()
    {
        // A procedural scene has no environment node until something asks for one, which this does.
        EnvironmentComponent environment = new NodeRef(Scene.GetEnvironmentNodeId()).Environment;
        if (!environment.Exists)
        {
            Log.Warn("[{{ProjectName}}] no environment node; default lighting will be used");
            return;
        }

        environment.HasSky = true;
        environment.SkyIdx = 0;
        environment.SkyIntensity = 300.0f;
        environment.HasSun = true;
        environment.SunIntensity = 300.0f;
        environment.SunRotation = 0.6f;
        environment.SunElevation = 0.9f;
    }

    /// <summary>
    /// Once per frame. Keep allocations out of here — set <c>GK_DOTNET_ALLOC_GUARD=1</c> while
    /// developing and the runtime will tell you when a tick allocates.
    /// </summary>
    protected override void OnTick(double deltaSeconds)
    {
        elapsedSeconds += deltaSeconds;

        if (spinning && NodeIds.IsValid(cubeNode))
        {
            spinAngle += (float)deltaSeconds * SpinSpeed;

            // Quaternion around Y, written out rather than built by a helper: there is no vector
            // math library on this side of the boundary, only the interop structs.
            float half = spinAngle * 0.5f;
            Vector4 rotation = new(0.0f, MathF.Sin(half), 0.0f, MathF.Cos(half));
            Scene.SetNodeRotation(cubeNode, in rotation);

            // Required once per tick by anything that moves a node: the scene graph is updated
            // immediately, but the renderer only re-uploads instance transforms when told to.
            Scene.MarkTransformDirty();
        }
    }

    /// <summary>Immediate-mode HUD. Return true to tell the host the frame was consumed.</summary>
    protected override bool OnRenderUI()
    {
        Vector2 screen = UI.GetScreenSize();

        UI.DrawRectFilled(20.0f, 20.0f, 300.0f, 96.0f, Color.FromBytes(18, 24, 32, 200), 10.0f);
        UI.DrawRect(20.0f, 20.0f, 300.0f, 96.0f, Color.FromBytes(255, 255, 255, 40), 10.0f, 1.0f);
        UI.DrawText("{{DisplayName}}", 36.0f, 34.0f, Color.White, 1.2f);
        UI.DrawText($"time {elapsedSeconds:F1}s", 36.0f, 62.0f, Color.FromBytes(168, 200, 214));
        UI.DrawText(spinning ? "SPACE: pause spin" : "SPACE: resume spin", 36.0f, 84.0f,
                    Color.FromBytes(124, 230, 168));

        // Drawn twice: HUD text sits over whatever the scene happens to be, and plain white is
        // invisible against a bright floor.
        const string hint = "ESC quits";
        Vector2 hintSize = UI.CalcTextSize(hint);
        float hintX = screen.X - hintSize.X - 20.0f;
        float hintY = screen.Y - hintSize.Y - 16.0f;
        UI.DrawText(hint, hintX + 1.0f, hintY + 1.0f, Color.FromBytes(10, 14, 20, 170));
        UI.DrawText(hint, hintX, hintY, Color.FromBytes(255, 255, 255, 190));
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
