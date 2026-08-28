using GkNext.Interop;

namespace GkNext;

/// <summary>
/// Sky and sun for a procedurally built scene.
/// </summary>
/// <remarks>
/// A scene built from <c>SceneBuild</c> starts with no environment node, so a game that skips this
/// gets whatever the engine defaults to and usually reads it as "my materials are wrong". The
/// setup is four properties and a null check, which is not interesting enough to write out in
/// every game — but it is not optional either, so it lives here rather than nowhere.
///
/// Call it from <c>OnSceneLoaded</c>. In <c>BeforeSceneRebuild</c> there is no node to address yet.
/// </remarks>
public static class Sky
{
    /// <summary>
    /// Lights the scene with a sky and a directional sun.
    /// </summary>
    /// <param name="skyIntensity">Ambient level from the sky dome. Lower this for an interior or
    /// for pale surfaces that would otherwise blow out to white.</param>
    /// <param name="sunIntensity">Strength of the directional light that casts the shadows.</param>
    /// <param name="sunRotation">Compass direction of the sun, in radians.</param>
    /// <param name="sunElevation">Height of the sun above the horizon, in radians. Low is a long
    /// shadow and a warm read; near π/2 is noon and almost no shadow at all.</param>
    /// <param name="skyIndex">Which of the built-in sky presets to use.</param>
    /// <returns>False when the scene has no environment node, having logged why.</returns>
    public static bool Apply(float skyIntensity = 300.0f,
                             float sunIntensity = 300.0f,
                             float sunRotation = 0.6f,
                             float sunElevation = 0.9f,
                             int skyIndex = 0)
    {
        EnvironmentComponent environment = new NodeRef(Scene.GetEnvironmentNodeId()).Environment;
        if (!environment.Exists)
        {
            Log.Warn("Sky.Apply: the scene has no environment node, so the engine default lighting " +
                     "is in use. This is expected before the scene is committed — call it from " +
                     "OnSceneLoaded, not BeforeSceneRebuild.");
            return false;
        }

        environment.HasSky = true;
        environment.SkyIdx = skyIndex;
        environment.SkyIntensity = skyIntensity;
        environment.HasSun = true;
        environment.SunIntensity = sunIntensity;
        environment.SunRotation = sunRotation;
        environment.SunElevation = sunElevation;
        return true;
    }
}
