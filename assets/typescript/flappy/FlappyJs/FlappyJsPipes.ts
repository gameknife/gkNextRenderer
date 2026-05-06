import * as NE from "../../Engine";
import type { FPipeConfig, FWorldConfig, Vec3 } from "../FlappyCommon";
import { FXorShift32 } from "./FlappyJsRng";

const f32 = Math.fround;
const hiddenY = -40.0;

interface FPipeRuntime {
    x: number;
    gapCenterY: number;
    active: boolean;
    scored: boolean;
    topNodeId?: number;
    bottomNodeId?: number;
}

function AabbOverlap(minAx: number, maxAx: number, minAy: number, maxAy: number,
                     minBx: number, maxBx: number, minBy: number, maxBy: number): boolean {
    return minAx <= maxBx && maxAx >= minBx && minAy <= maxBy && maxAy >= minBy;
}

function SetVisible(nodeId: number, visible: boolean): void {
    const node = NE.Global.GetScene().GetNodeById(nodeId);
    if (!node) {
        return;
    }

    const render = node.GetComponent("RenderComponent") as NE.RenderComponent;
    if (render) {
        render.Visible = visible;
    }
}

export class FFlappyJsPipes {
    private pipes: FPipeRuntime[] = [];
    private spawnTimer = 0.0;

    Reset(config: FPipeConfig): void {
        const desiredSize = Math.max(1, config.poolSize);
        while (this.pipes.length < desiredSize) {
            this.pipes.push({ x: 0.0, gapCenterY: 0.0, active: false, scored: false });
        }
        this.pipes.length = desiredSize;
        this.spawnTimer = f32(config.spawnInterval);
        for (const pipe of this.pipes) {
            this.Hide(pipe);
        }
    }

    SetNodes(index: number, topNodeId: number, bottomNodeId: number): void {
        while (index >= this.pipes.length) {
            this.pipes.push({ x: 0.0, gapCenterY: 0.0, active: false, scored: false });
        }

        this.pipes[index].topNodeId = topNodeId;
        this.pipes[index].bottomNodeId = bottomNodeId;
        this.Hide(this.pipes[index]);
    }

    Update(fixedDeltaSeconds: number, config: FPipeConfig, world: FWorldConfig, rng: FXorShift32): void {
        this.spawnTimer = f32(this.spawnTimer - fixedDeltaSeconds);
        if (this.spawnTimer <= 0.0) {
            this.SpawnPipe(config, world, rng);
            this.spawnTimer = f32(this.spawnTimer + config.spawnInterval);
        }

        for (const pipe of this.pipes) {
            if (!pipe.active) {
                continue;
            }

            pipe.x = f32(pipe.x - f32(config.speed * fixedDeltaSeconds));
            if (pipe.x < config.destroyX) {
                this.Hide(pipe);
                continue;
            }

            this.SyncVisual(pipe, config, world);
        }
    }

    CheckCollision(birdPosition: Vec3, birdRadius: number, config: FPipeConfig): boolean {
        const birdMinX = f32(birdPosition.x - birdRadius);
        const birdMaxX = f32(birdPosition.x + birdRadius);
        const birdMinY = f32(birdPosition.y - birdRadius);
        const birdMaxY = f32(birdPosition.y + birdRadius);
        const halfWidth = f32(config.width * 0.5);
        const halfGap = f32(config.gapHeight * 0.5);

        for (const pipe of this.pipes) {
            if (!pipe.active) {
                continue;
            }

            const pipeMinX = f32(pipe.x - halfWidth);
            const pipeMaxX = f32(pipe.x + halfWidth);
            const hitsTop = AabbOverlap(birdMinX, birdMaxX, birdMinY, birdMaxY,
                                        pipeMinX, pipeMaxX, f32(pipe.gapCenterY + halfGap), Number.MAX_VALUE);
            const hitsBottom = AabbOverlap(birdMinX, birdMaxX, birdMinY, birdMaxY,
                                           pipeMinX, pipeMaxX, -Number.MAX_VALUE, f32(pipe.gapCenterY - halfGap));
            if (hitsTop || hitsBottom) {
                return true;
            }
        }
        return false;
    }

    ConsumeScoreEvents(birdX: number): number {
        let scoreEvents = 0;
        for (const pipe of this.pipes) {
            if (pipe.active && !pipe.scored && birdX > pipe.x) {
                pipe.scored = true;
                scoreEvents += 1;
            }
        }
        return scoreEvents;
    }

    private SpawnPipe(config: FPipeConfig, world: FWorldConfig, rng: FXorShift32): void {
        const pipe = this.pipes.find((candidate) => !candidate.active);
        if (!pipe) {
            return;
        }

        const t = rng.NextFloat01();
        pipe.x = f32(config.spawnX);
        pipe.gapCenterY = f32(config.gapCenterMinY + f32(f32(config.gapCenterMaxY - config.gapCenterMinY) * t));
        pipe.active = true;
        pipe.scored = false;
        if (pipe.topNodeId !== undefined) {
            SetVisible(pipe.topNodeId, true);
        }
        if (pipe.bottomNodeId !== undefined) {
            SetVisible(pipe.bottomNodeId, true);
        }
        this.SyncVisual(pipe, config, world);
    }

    private SyncVisual(pipe: FPipeRuntime, config: FPipeConfig, world: FWorldConfig): void {
        const halfGap = f32(config.gapHeight * 0.5);
        const topBottomY = f32(pipe.gapCenterY + halfGap);
        const bottomTopY = f32(pipe.gapCenterY - halfGap);
        const scene = NE.Global.GetScene();
        const topNode = pipe.topNodeId === undefined ? undefined : scene.GetNodeById(pipe.topNodeId);
        const bottomNode = pipe.bottomNodeId === undefined ? undefined : scene.GetNodeById(pipe.bottomNodeId);

        if (topNode) {
            const topCenterY = f32((world.maxY + topBottomY) * 0.5);
            const topHeight = Math.max(0.01, f32(world.maxY - topBottomY));
            topNode.Translation = { x: pipe.x, y: topCenterY, z: world.gameplayZ };
            topNode.Scale = { x: config.width, y: topHeight, z: 0.35 };
            topNode.RecalcTransform(true);
        }

        if (bottomNode) {
            const bottomCenterY = f32((world.minY + bottomTopY) * 0.5);
            const bottomHeight = Math.max(0.01, f32(bottomTopY - world.minY));
            bottomNode.Translation = { x: pipe.x, y: bottomCenterY, z: world.gameplayZ };
            bottomNode.Scale = { x: config.width, y: bottomHeight, z: 0.35 };
            bottomNode.RecalcTransform(true);
        }
    }

    private Hide(pipe: FPipeRuntime): void {
        pipe.active = false;
        pipe.scored = false;
        pipe.x = 0.0;
        pipe.gapCenterY = 0.0;

        const scene = NE.Global.GetScene();
        const topNode = pipe.topNodeId === undefined ? undefined : scene.GetNodeById(pipe.topNodeId);
        const bottomNode = pipe.bottomNodeId === undefined ? undefined : scene.GetNodeById(pipe.bottomNodeId);

        if (topNode) {
            topNode.Translation = { x: 0.0, y: hiddenY, z: 0.0 };
            topNode.RecalcTransform(true);
            SetVisible(pipe.topNodeId as number, false);
        }

        if (bottomNode) {
            bottomNode.Translation = { x: 0.0, y: hiddenY, z: 0.0 };
            bottomNode.RecalcTransform(true);
            SetVisible(pipe.bottomNodeId as number, false);
        }
    }
}
