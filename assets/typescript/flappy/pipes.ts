import type { GameplayConfig } from "./config";
import { XorShift32 } from "./rng";

const f32 = Math.fround;

interface PipeRuntime {
    x: number;
    gapCenterY: number;
    active: boolean;
    scored: boolean;
    topNodeId: number;
    bottomNodeId: number;
}

function overlap(minAx: number, maxAx: number, minAy: number, maxAy: number, minBx: number, maxBx: number, minBy: number, maxBy: number): boolean {
    return minAx <= maxBx && maxAx >= minBx && minAy <= maxBy && maxAy >= minBy;
}

export class Pipes {
    pipes: PipeRuntime[] = [];
    private spawnTimer = 0.0;

    reset(config: GameplayConfig): void {
        this.spawnTimer = f32(config.pipe.spawnInterval);
        for (const pipe of this.pipes) {
            this.hide(pipe);
        }
    }

    addPipePair(topNodeId: number, bottomNodeId: number): void {
        this.pipes.push({ x: 0.0, gapCenterY: 0.0, active: false, scored: false, topNodeId, bottomNodeId });
    }

    update(dt: number, config: GameplayConfig, rng: XorShift32): void {
        this.spawnTimer = f32(this.spawnTimer - dt);
        if (this.spawnTimer <= 0.0) {
            this.spawn(config, rng);
            this.spawnTimer = f32(this.spawnTimer + config.pipe.spawnInterval);
        }

        for (const pipe of this.pipes) {
            if (!pipe.active) {
                continue;
            }
            pipe.x = f32(pipe.x - f32(config.pipe.speed * dt));
            if (pipe.x < config.pipe.destroyX) {
                this.hide(pipe);
                continue;
            }
            this.sync(pipe, config);
        }
    }

    consumeScoreEvents(birdX: number): number {
        let count = 0;
        for (const pipe of this.pipes) {
            if (pipe.active && !pipe.scored && birdX > pipe.x) {
                pipe.scored = true;
                count += 1;
            }
        }
        return count;
    }

    checkCollision(birdX: number, birdY: number, radius: number, config: GameplayConfig): boolean {
        const birdMinX = f32(birdX - radius);
        const birdMaxX = f32(birdX + radius);
        const birdMinY = f32(birdY - radius);
        const birdMaxY = f32(birdY + radius);
        const halfWidth = f32(config.pipe.width * 0.5);
        const halfGap = f32(config.pipe.gapHeight * 0.5);
        for (const pipe of this.pipes) {
            if (!pipe.active) {
                continue;
            }
            const pipeMinX = f32(pipe.x - halfWidth);
            const pipeMaxX = f32(pipe.x + halfWidth);
            if (overlap(birdMinX, birdMaxX, birdMinY, birdMaxY, pipeMinX, pipeMaxX, f32(pipe.gapCenterY + halfGap), Number.MAX_VALUE) ||
                overlap(birdMinX, birdMaxX, birdMinY, birdMaxY, pipeMinX, pipeMaxX, -Number.MAX_VALUE, f32(pipe.gapCenterY - halfGap))) {
                return true;
            }
        }
        return false;
    }

    private spawn(config: GameplayConfig, rng: XorShift32): void {
        const pipe = this.pipes.find((candidate) => !candidate.active);
        if (!pipe) {
            return;
        }
        const t = rng.nextFloat01();
        pipe.x = f32(config.pipe.spawnX);
        pipe.gapCenterY = f32(config.pipe.gapCenterMinY + f32(f32(config.pipe.gapCenterMaxY - config.pipe.gapCenterMinY) * t));
        pipe.active = true;
        pipe.scored = false;
        this.sync(pipe, config);
    }

    private sync(pipe: PipeRuntime, config: GameplayConfig): void {
        const scene = (globalThis as any).__flappyScene;
        const halfGap = f32(config.pipe.gapHeight * 0.5);
        const topBottomY = f32(pipe.gapCenterY + halfGap);
        const bottomTopY = f32(pipe.gapCenterY - halfGap);
        const topNode = scene.GetNodeById(pipe.topNodeId) as any;
        const bottomNode = scene.GetNodeById(pipe.bottomNodeId) as any;
        if (topNode) {
            const topCenterY = f32((config.world.maxY + topBottomY) * 0.5);
            const topHeight = Math.max(0.01, f32(config.world.maxY - topBottomY));
            topNode.Translation = { x: pipe.x, y: topCenterY, z: config.world.gameplayZ };
            topNode.Scale = { x: config.pipe.width, y: topHeight, z: 0.35 };
            topNode.RecalcTransform(true);
        }
        if (bottomNode) {
            const bottomCenterY = f32((config.world.minY + bottomTopY) * 0.5);
            const bottomHeight = Math.max(0.01, f32(bottomTopY - config.world.minY));
            bottomNode.Translation = { x: pipe.x, y: bottomCenterY, z: config.world.gameplayZ };
            bottomNode.Scale = { x: config.pipe.width, y: bottomHeight, z: 0.35 };
            bottomNode.RecalcTransform(true);
        }
    }

    private hide(pipe: PipeRuntime): void {
        pipe.active = false;
        pipe.scored = false;
        pipe.x = 0.0;
        pipe.gapCenterY = 0.0;
        const scene = (globalThis as any).__flappyScene;
        const topNode = scene ? scene.GetNodeById(pipe.topNodeId) as any : undefined;
        const bottomNode = scene ? scene.GetNodeById(pipe.bottomNodeId) as any : undefined;
        if (topNode) {
            topNode.Translation = { x: 0.0, y: -40.0, z: 0.0 };
            topNode.RecalcTransform(true);
        }
        if (bottomNode) {
            bottomNode.Translation = { x: 0.0, y: -40.0, z: 0.0 };
            bottomNode.RecalcTransform(true);
        }
    }
}
