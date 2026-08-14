using GkNext;
using GkNext.Interop;

namespace Flappy;

/// <summary>Direct translation of FFlappyCppBird. Same operations, same order, same float type.</summary>
public sealed class FlappyBird
{
    private Vector3 position = new(-3.0f, 0.0f, 0.0f);
    private float velocityY;
    private uint nodeId = NodeIds.Invalid;

    public Vector3 Position => position;
    public float VelocityY => velocityY;

    public void SetNode(uint id) => nodeId = id;

    public void Reset(in BirdConfig config)
    {
        position = config.InitialPosition;
        velocityY = 0.0f;
        SyncVisual();
    }

    public void Flap(in BirdConfig config)
    {
        velocityY = config.FlapVelocity;
    }

    public void Update(float fixedDeltaSeconds, in BirdConfig config)
    {
        velocityY = Math.Clamp(velocityY + config.Gravity * fixedDeltaSeconds,
                               config.MinVelocity,
                               config.MaxVelocity);
        position.Y += velocityY * fixedDeltaSeconds;
        SyncVisual();
    }

    public void SyncVisual()
    {
        if (NodeIds.IsValid(nodeId))
        {
            Scene.SetNodeTranslation(nodeId, in position);
        }
    }
}
