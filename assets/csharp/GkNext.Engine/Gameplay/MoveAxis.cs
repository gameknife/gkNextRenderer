namespace GkNext;

/// <summary>
/// A movement stick: how far right and how far forward the player is asking to go, in [-1, 1].
/// </summary>
/// <remarks>
/// The value is already normalised, which is the whole reason this type exists. Summing four key
/// states gives (1, 1) on the diagonal — a length of 1.41 — so a player holding W and D moves 41%
/// faster than one holding W. Every game that wrote the key ladder out by hand had to remember to
/// divide by the length afterwards, and the ones that forgot shipped with the bug.
///
/// It carries intent, not direction: a game maps <see cref="Forward"/> onto its own world axis or
/// camera basis. A top-down game reads it as -Z, a camera-relative one multiplies it into the
/// camera's flat forward, and neither has to agree with the other.
/// </remarks>
public readonly struct MoveAxis
{
    /// <summary>Right on the screen or the camera. D, the right arrow, or the stick's X.</summary>
    public readonly float Right;

    /// <summary>Away from the player. W, the up arrow, or the stick pushed forward.</summary>
    public readonly float Forward;

    public MoveAxis(float right, float forward)
    {
        float length = MathF.Sqrt(right * right + forward * forward);
        if (length > 1.0f)
        {
            right /= length;
            forward /= length;
        }
        Right = right;
        Forward = forward;
    }

    /// <summary>True when the player is asking to move at all.</summary>
    public bool IsMoving => Right != 0.0f || Forward != 0.0f;

    /// <summary>
    /// How hard the stick is pushed, in [0, 1]. A gamepad can ask for a walk; a keyboard only ever
    /// returns 0 or 1.
    /// </summary>
    public float Magnitude => MathF.Sqrt(Right * Right + Forward * Forward);

    /// <summary>The angle the player is asking for, measured the same way a yaw is.</summary>
    public float Heading => MathF.Atan2(Right, Forward);

    /// <summary>
    /// Reads the stick for this frame: WASD, the arrow keys, and the gamepad's left stick.
    /// </summary>
    /// <remarks>
    /// Polled rather than driven by <c>OnInputEvent</c>, because holding a key is a state and not
    /// an event. An event stream delivers one press and then the operating system's key repeat,
    /// which produces a stutter rather than continuous motion.
    ///
    /// The gamepad is read here rather than left to each game to remember. A managed game that
    /// handles only the keyboard looks completely fine until someone picks up a controller.
    /// </remarks>
    /// <param name="arrowKeys">Also accept the arrow keys. Turn this off when they mean something
    /// else, such as menu navigation drawn over the game.</param>
    /// <param name="gamepad">Also accept the left stick.</param>
    /// <param name="deadZone">Stick movement below this is treated as zero; sticks rest slightly
    /// off centre and would otherwise walk the player away on their own.</param>
    public static MoveAxis Poll(bool arrowKeys = true, bool gamepad = true, float deadZone = 0.18f)
    {
        float right = 0.0f;
        float forward = 0.0f;

        if (Input.IsKeyDown("d") || (arrowKeys && Input.IsKeyDown("right")))
        {
            right += 1.0f;
        }
        if (Input.IsKeyDown("a") || (arrowKeys && Input.IsKeyDown("left")))
        {
            right -= 1.0f;
        }
        if (Input.IsKeyDown("w") || (arrowKeys && Input.IsKeyDown("up")))
        {
            forward += 1.0f;
        }
        if (Input.IsKeyDown("s") || (arrowKeys && Input.IsKeyDown("down")))
        {
            forward -= 1.0f;
        }

        if (gamepad)
        {
            float stickX = Input.GetGamepadAxis(0);
            // Negated: the stick reports Y downwards, which is backwards for the player.
            float stickY = -Input.GetGamepadAxis(1);
            if (MathF.Sqrt(stickX * stickX + stickY * stickY) > deadZone)
            {
                right += stickX;
                forward += stickY;
            }
        }

        return new MoveAxis(right, forward);
    }
}
