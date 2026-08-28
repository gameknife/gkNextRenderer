using GkNext.Interop;

namespace GkNext;

/// <summary>
/// The colours the built-in HUD helpers and the game templates draw with.
/// </summary>
/// <remarks>
/// Named roles rather than a palette to admire: <c>Danger</c> says what the colour means, so a
/// game that restyles its HUD changes one file instead of hunting for the particular shade of red
/// it typed into forty draw calls. Replace these values, or stop using the type — nothing in the
/// engine reads them except <see cref="ManagedImGui"/>'s text shadow.
/// </remarks>
public static class HudPalette
{
    /// <summary>Ordinary readable text.</summary>
    public static Color Text => Color.FromBytes(226, 236, 242);

    /// <summary>Secondary text: counters, coordinates, anything glanced at rather than read.</summary>
    public static Color Muted => Color.FromBytes(156, 182, 198);

    /// <summary>What the player should do next. "PRESS ANY KEY", "SPACE TO RESTART".</summary>
    public static Color Accent => Color.FromBytes(124, 230, 168);

    /// <summary>Titles and the one number that matters.</summary>
    public static Color Highlight => Color.FromBytes(255, 224, 128);

    /// <summary>Death, damage, an empty magazine.</summary>
    public static Color Danger => Color.FromBytes(255, 132, 120);

    /// <summary>The offset copy drawn under HUD text so it survives a bright background.</summary>
    public static Color Shadow => Color.FromBytes(8, 12, 18, 175);

    /// <summary>A resource bar that is doing fine.</summary>
    public static Color BarHealthy => Color.FromBytes(96, 210, 140);

    /// <summary>A resource bar that is not.</summary>
    public static Color BarCritical => Color.FromBytes(226, 96, 88);

    /// <summary>
    /// Bar colour for a fraction in [0, 1], turning critical near the bottom.
    /// </summary>
    /// <remarks>
    /// A threshold rather than a gradient: a bar that fades continuously never tells the player
    /// the moment they are in trouble, and that moment is the only thing the colour is for.
    /// </remarks>
    public static Color Bar(float fraction, float criticalBelow = 0.35f)
        => fraction > criticalBelow ? BarHealthy : BarCritical;
}
