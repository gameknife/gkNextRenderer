using GkNext;
using GkNext.Interop;

namespace {{Namespace}};

/// <summary>
/// The weapon: a magazine, two timers and the tracer that shows where a shot went.
/// </summary>
/// <remarks>
/// Split out of the game class because it is the part most likely to be replaced first. A second
/// weapon is another instance with different numbers, and nothing in the game class changes except
/// which one it fires.
///
/// It deliberately knows nothing about targets. <see cref="TryFire"/> answers only "did a round
/// leave the barrel"; who it hit is the game's decision, and keeping that out of here is what lets
/// the same weapon serve a hitscan, a projectile or a shotgun.
/// </remarks>
internal sealed class Rifle
{
    /// <summary>How long the tracer stays visible. Four frames at 60fps — long enough to read as a
    /// line, short enough that two shots never overlap.</summary>
    private const float TracerSeconds = 0.05f;

    private const float TracerThickness = 0.05f;

    private readonly int magazineSize;
    private readonly float fireInterval;
    private readonly float reloadSeconds;

    private uint tracerNode = NodeIds.Invalid;
    private float fireCooldown;
    private float reloadTimer;
    private float tracerTimer;

    public Rifle(int magazineSize, float fireInterval, float reloadSeconds)
    {
        this.magazineSize = magazineSize;
        this.fireInterval = fireInterval;
        this.reloadSeconds = reloadSeconds;
        Ammo = magazineSize;
    }

    public int MagazineSize => magazineSize;
    public float ReloadSeconds => reloadSeconds;
    public int Ammo { get; private set; }
    public bool IsReloading => reloadTimer > 0.0f;
    public bool IsEmpty => Ammo <= 0 && !IsReloading;

    /// <summary>How far through a reload, in [0, 1]. Drive a HUD sweep with it.</summary>
    public float ReloadProgress => IsReloading ? 1.0f - reloadTimer / reloadSeconds : 1.0f;

    /// <summary>The node the tracer is drawn with, built once during the scene build.</summary>
    public void SetTracerNode(uint node) => tracerNode = node;

    public void Reset()
    {
        Ammo = magazineSize;
        fireCooldown = 0.0f;
        reloadTimer = 0.0f;
        HideTracer();
    }

    /// <summary>Advances the cooldowns. Call once per frame, before <see cref="TryFire"/>.</summary>
    public void Tick(float deltaSeconds)
    {
        fireCooldown = MathF.Max(0.0f, fireCooldown - deltaSeconds);

        if (IsReloading)
        {
            reloadTimer -= deltaSeconds;
            if (reloadTimer <= 0.0f)
            {
                reloadTimer = 0.0f;
                Ammo = magazineSize;
            }
        }

        if (tracerTimer > 0.0f)
        {
            tracerTimer -= deltaSeconds;
            if (tracerTimer <= 0.0f)
            {
                HideTracer();
            }
        }
    }

    /// <summary>
    /// Consumes a round if one is chambered and the weapon is ready.
    /// </summary>
    /// <remarks>
    /// Pulling the trigger on an empty magazine starts a reload rather than doing nothing. Making
    /// the player press R to discover the magazine is empty is a worse game than telling them by
    /// reloading; that decision belongs to the weapon, so it lives here.
    /// </remarks>
    public bool TryFire()
    {
        if (fireCooldown > 0.0f || IsReloading)
        {
            return false;
        }
        if (Ammo <= 0)
        {
            BeginReload();
            return false;
        }

        Ammo--;
        fireCooldown = fireInterval;
        return true;
    }

    /// <summary>Starts a reload. Ignored when one is already running or the magazine is full.</summary>
    public bool BeginReload()
    {
        if (IsReloading || Ammo >= magazineSize)
        {
            return false;
        }
        reloadTimer = reloadSeconds;
        return true;
    }

    /// <summary>
    /// Stretches the single tracer node between two points.
    /// </summary>
    /// <remarks>
    /// One node, reused for every shot. A bullet that exists for four frames does not deserve a
    /// node built and destroyed each time it is fired — that rebuilds acceleration structures mid
    /// firefight, which is exactly when the frame budget is tightest.
    /// </remarks>
    public void ShowTracer(in Vector3 from, in Vector3 to)
    {
        if (!NodeIds.IsValid(tracerNode))
        {
            return;
        }

        Vector3 delta = new(to.X - from.X, to.Y - from.Y, to.Z - from.Z);
        float length = MathF.Sqrt(delta.X * delta.X + delta.Y * delta.Y + delta.Z * delta.Z);
        if (length < 0.01f)
        {
            return;
        }

        // The unit box is one deep along +Z, so scaling Z by the length and pointing +Z down the
        // shot turns it into a beam between the two points.
        Scene.SetNodeTranslation(tracerNode, new Vector3(from.X + delta.X * 0.5f,
                                                         from.Y + delta.Y * 0.5f,
                                                         from.Z + delta.Z * 0.5f));
        Scene.SetNodeScale(tracerNode, new Vector3(TracerThickness, TracerThickness, length));
        Scene.SetNodeRotation(tracerNode, Quat.LookAlong(delta));
        Scene.SetNodeVisible(tracerNode, true);
        Scene.MarkTransformDirty();
        tracerTimer = TracerSeconds;
    }

    private void HideTracer()
    {
        tracerTimer = 0.0f;
        if (NodeIds.IsValid(tracerNode))
        {
            Scene.SetNodeVisible(tracerNode, false);
        }
    }
}
