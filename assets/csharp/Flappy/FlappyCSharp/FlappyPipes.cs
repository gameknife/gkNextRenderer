using GkNext;
using GkNext.Interop;

namespace Flappy;

/// <summary>
/// Direct translation of FFlappyCppPipes, including the pool semantics: pipes are reused in place
/// and the first inactive slot is the one that spawns, because which slot spawns decides nothing
/// but the order of RNG draws must match exactly.
/// </summary>
public sealed class FlappyPipes
{
    private const float HiddenY = -40.0f;

    private struct Pipe
    {
        public float X;
        public float GapCenterY;
        public bool Active;
        public bool Scored;
        public uint TopNodeId;
        public uint BottomNodeId;
    }

    private static bool HasNode(uint nodeId) => NodeIds.IsValid(nodeId);

    private Pipe[] pipes = [];
    private float spawnTimer;

    public int Count => pipes.Length;

    public void Reset(in PipeConfig config)
    {
        int desired = Math.Max(1, config.PoolSize);
        if (pipes.Length != desired)
        {
            Pipe[] resized = new Pipe[desired];
            int carried = Math.Min(pipes.Length, desired);
            Array.Copy(pipes, resized, carried);
            // A zeroed slot would claim node id 0, which is a real node. Slots start out holding
            // no node until SetNodes fills them in.
            for (int index = carried; index < desired; index++)
            {
                resized[index].TopNodeId = NodeIds.Invalid;
                resized[index].BottomNodeId = NodeIds.Invalid;
            }
            pipes = resized;
        }

        spawnTimer = config.SpawnInterval;
        for (int index = 0; index < pipes.Length; index++)
        {
            Hide(ref pipes[index]);
        }
    }

    /// <summary>
    /// Records the nodes a pool slot drives. Called from BeforeSceneRebuild, so it deliberately
    /// touches nothing on the live scene: at that point the nodes exist only in the vector being
    /// built and Scene.* cannot see them yet. Their hidden starting position comes from the spec
    /// they were built with; Reset() hides them for real once the scene is committed.
    /// </summary>
    public void SetNodes(int index, uint topNodeId, uint bottomNodeId)
    {
        if (index < 0 || index >= pipes.Length)
        {
            return;
        }
        pipes[index].TopNodeId = topNodeId;
        pipes[index].BottomNodeId = bottomNodeId;
        pipes[index].Active = false;
        pipes[index].Scored = false;
    }

    public void Update(float fixedDeltaSeconds, in PipeConfig config, in WorldConfig world, XorShift32 rng)
    {
        spawnTimer -= fixedDeltaSeconds;
        if (spawnTimer <= 0.0f)
        {
            SpawnPipe(in config, in world, rng);
            spawnTimer += config.SpawnInterval;
        }

        for (int index = 0; index < pipes.Length; index++)
        {
            ref Pipe pipe = ref pipes[index];
            if (!pipe.Active)
            {
                continue;
            }

            pipe.X -= config.Speed * fixedDeltaSeconds;
            if (pipe.X < config.DestroyX)
            {
                Hide(ref pipe);
                continue;
            }

            SyncVisual(ref pipe, in config, in world);
        }
    }

    public bool CheckCollision(in Vector3 birdPosition, float birdRadius, in PipeConfig config)
    {
        float birdMinX = birdPosition.X - birdRadius;
        float birdMaxX = birdPosition.X + birdRadius;
        float birdMinY = birdPosition.Y - birdRadius;
        float birdMaxY = birdPosition.Y + birdRadius;
        float halfWidth = config.Width * 0.5f;
        float halfGap = config.GapHeight * 0.5f;

        foreach (ref readonly Pipe pipe in pipes.AsSpan())
        {
            if (!pipe.Active)
            {
                continue;
            }

            float pipeMinX = pipe.X - halfWidth;
            float pipeMaxX = pipe.X + halfWidth;
            bool hitsTop = Overlaps(birdMinX, birdMaxX, birdMinY, birdMaxY,
                                    pipeMinX, pipeMaxX, pipe.GapCenterY + halfGap, float.MaxValue);
            bool hitsBottom = Overlaps(birdMinX, birdMaxX, birdMinY, birdMaxY,
                                       pipeMinX, pipeMaxX, float.MinValue, pipe.GapCenterY - halfGap);
            if (hitsTop || hitsBottom)
            {
                return true;
            }
        }
        return false;
    }

    public int ConsumeScoreEvents(float birdX)
    {
        int scoreEvents = 0;
        for (int index = 0; index < pipes.Length; index++)
        {
            ref Pipe pipe = ref pipes[index];
            if (pipe.Active && !pipe.Scored && birdX > pipe.X)
            {
                pipe.Scored = true;
                scoreEvents++;
            }
        }
        return scoreEvents;
    }

    private void SpawnPipe(in PipeConfig config, in WorldConfig world, XorShift32 rng)
    {
        int slot = -1;
        for (int index = 0; index < pipes.Length; index++)
        {
            if (!pipes[index].Active)
            {
                slot = index;
                break;
            }
        }
        if (slot < 0)
        {
            return;
        }

        // The RNG draw happens only after a free slot is found, matching the C++ order. Drawing
        // first would desynchronise the whole replay the moment the pool fills up.
        float t = rng.NextFloat01();
        ref Pipe pipe = ref pipes[slot];
        pipe.X = config.SpawnX;
        pipe.GapCenterY = config.GapCenterMinY + (config.GapCenterMaxY - config.GapCenterMinY) * t;
        pipe.Active = true;
        pipe.Scored = false;

        if (HasNode(pipe.TopNodeId))
        {
            Scene.SetNodeVisible(pipe.TopNodeId, true);
        }
        if (HasNode(pipe.BottomNodeId))
        {
            Scene.SetNodeVisible(pipe.BottomNodeId, true);
        }
        SyncVisual(ref pipe, in config, in world);
    }

    private static void SyncVisual(ref Pipe pipe, in PipeConfig config, in WorldConfig world)
    {
        float halfGap = config.GapHeight * 0.5f;
        float topBottomY = pipe.GapCenterY + halfGap;
        float bottomTopY = pipe.GapCenterY - halfGap;

        if (HasNode(pipe.TopNodeId))
        {
            float topCenterY = (world.MaxY + topBottomY) * 0.5f;
            float topHeight = Math.Max(0.01f, world.MaxY - topBottomY);
            Vector3 translation = new(pipe.X, topCenterY, world.GameplayZ);
            Vector3 scale = new(1.0f, topHeight, 1.0f);
            Scene.SetNodeTranslation(pipe.TopNodeId, in translation);
            Scene.SetNodeScale(pipe.TopNodeId, in scale);
        }

        if (HasNode(pipe.BottomNodeId))
        {
            float bottomCenterY = (world.MinY + bottomTopY) * 0.5f;
            float bottomHeight = Math.Max(0.01f, bottomTopY - world.MinY);
            Vector3 translation = new(pipe.X, bottomCenterY, world.GameplayZ);
            Vector3 scale = new(1.0f, bottomHeight, 1.0f);
            Scene.SetNodeTranslation(pipe.BottomNodeId, in translation);
            Scene.SetNodeScale(pipe.BottomNodeId, in scale);
        }
    }

    private static void Hide(ref Pipe pipe)
    {
        pipe.Active = false;
        pipe.Scored = false;
        pipe.X = 0.0f;
        pipe.GapCenterY = 0.0f;

        Vector3 hidden = new(0.0f, HiddenY, 0.0f);
        if (HasNode(pipe.TopNodeId))
        {
            Scene.SetNodeTranslation(pipe.TopNodeId, in hidden);
            Scene.SetNodeVisible(pipe.TopNodeId, false);
        }
        if (HasNode(pipe.BottomNodeId))
        {
            Scene.SetNodeTranslation(pipe.BottomNodeId, in hidden);
            Scene.SetNodeVisible(pipe.BottomNodeId, false);
        }
    }

    private static bool Overlaps(float minAx, float maxAx, float minAy, float maxAy,
                                 float minBx, float maxBx, float minBy, float maxBy)
        => minAx <= maxBx && maxAx >= minBx && minAy <= maxBy && maxAy >= minBy;
}
