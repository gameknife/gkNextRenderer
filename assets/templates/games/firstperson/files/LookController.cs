using GkNext;
using GkNext.Interop;

namespace {{Namespace}};

/// <summary>
/// Yaw and pitch, turned into a camera basis.
/// </summary>
/// <remarks>
/// Look is drag-based: hold the right mouse button and move. That is deliberate rather than a
/// stopgap — there is no relative mouse mode on this side of the boundary, and a game that grabbed
/// the pointer would be unusable inside the editor viewport, where the same code has to run.
///
/// Angles are stored, not a matrix. Everything else is derived, which keeps the one piece of state
/// that can drift down to two floats.
/// </remarks>
internal sealed class LookController(float sensitivity)
{
    /// <summary>SDL button numbering, which is what the input bindings carry: 1 left, 2 middle,
    /// 3 right.</summary>
    private const int RightMouseButton = 3;

    /// <summary>Just under a right angle: at exactly 90 degrees the forward vector is parallel to
    /// world up and the camera basis collapses.</summary>
    private const float PitchLimit = 1.55f;

    private bool dragging;
    private Vector2 lastMousePosition;

    public float Yaw { get; private set; }
    public float Pitch { get; private set; } = -0.15f;

    /// <summary>Where the camera is pointing.</summary>
    public Vector3 Forward
    {
        get
        {
            float cosPitch = MathF.Cos(Pitch);
            return new Vector3(MathF.Sin(Yaw) * cosPitch, MathF.Sin(Pitch), -MathF.Cos(Yaw) * cosPitch);
        }
    }

    /// <summary>Camera right, flat on the ground plane — strafing should not climb.</summary>
    public Vector3 Right => new(MathF.Cos(Yaw), 0.0f, MathF.Sin(Yaw));

    /// <summary>Forward with the pitch removed, so walking forward while looking down still walks
    /// forward at the same speed.</summary>
    public Vector3 FlatForward => new(MathF.Sin(Yaw), 0.0f, -MathF.Cos(Yaw));

    public void Reset(float yaw, float pitch)
    {
        Yaw = yaw;
        Pitch = pitch;
        dragging = false;
    }

    /// <summary>Call once per frame, before the camera is read.</summary>
    public void Update()
    {
        Vector2 mouse = Input.GetMousePosition();
        bool held = Input.IsMouseButtonDown(RightMouseButton);

        // The frame the drag starts contributes no rotation: without this the first frame uses a
        // stale anchor and the view snaps by however far the pointer travelled since the last drag.
        if (held && !dragging)
        {
            dragging = true;
            lastMousePosition = mouse;
            return;
        }

        if (!held)
        {
            dragging = false;
            return;
        }

        float deltaX = mouse.X - lastMousePosition.X;
        float deltaY = mouse.Y - lastMousePosition.Y;
        lastMousePosition = mouse;

        Yaw += deltaX * sensitivity;
        Pitch -= deltaY * sensitivity;
        Pitch = MathF.Max(-PitchLimit, MathF.Min(PitchLimit, Pitch));
    }
}
