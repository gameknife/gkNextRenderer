using GkNext;
using GkNext.Interop;

namespace Flappy;

/// <summary>
/// One scrolling backdrop layer: mountains, vegetation or clouds.
/// </summary>
/// <remarks>
/// Purely cosmetic — it moves backdrop nodes and never touches the simulation, so it cannot affect
/// replay parity. It exists because FlappyCSharp is meant to be the C# counterpart of FlappyCpp,
/// and a side-by-side comparison is only meaningful if both draw the same game.
///
/// Positions are tracked here rather than read back from the engine: the binding surface has no
/// node-translation getter, and keeping the authoritative value on this side avoids a per-frame
/// round trip for data the layer already knows.
/// </remarks>
public sealed class ParallaxLayer(float speed, float spacing, float z)
{
    private uint[] nodeIds = [];
    private float[] x = [];
    private float[] y = [];

    public void Reset(int count)
    {
        nodeIds = new uint[count];
        x = new float[count];
        y = new float[count];
        for (int index = 0; index < count; index++)
        {
            nodeIds[index] = NodeIds.Invalid;
        }
    }

    public void SetNode(int index, uint nodeId, float startX, float startY)
    {
        if (index < 0 || index >= nodeIds.Length)
        {
            return;
        }
        nodeIds[index] = nodeId;
        x[index] = startX;
        y[index] = startY;
    }

    public void Update(float deltaSeconds)
    {
        if (nodeIds.Length == 0)
        {
            return;
        }

        float wrapWidth = nodeIds.Length * spacing;
        float wrapMinX = -0.5f * wrapWidth;

        for (int index = 0; index < nodeIds.Length; index++)
        {
            if (!NodeIds.IsValid(nodeIds[index]))
            {
                continue;
            }

            x[index] -= speed * deltaSeconds;
            if (x[index] < wrapMinX)
            {
                x[index] += wrapWidth;
            }

            Vector3 translation = new(x[index], y[index], z);
            Scene.SetNodeTranslation(nodeIds[index], in translation);
        }
    }
}
