import type { GameplayConfig, Vec3 } from "./config";

const f32 = Math.fround;

export class Bird {
    position: Vec3 = { x: -3.0, y: 0.0, z: 0.0 };
    velocityY = 0.0;
    nodeId = 0;

    reset(config: GameplayConfig): void {
        this.position = { ...config.bird.initialPosition };
        this.velocityY = 0.0;
        this.sync();
    }

    flap(config: GameplayConfig): void {
        this.velocityY = f32(config.bird.flapVelocity);
    }

    update(dt: number, config: GameplayConfig): void {
        const nextVelocity = f32(this.velocityY + f32(config.bird.gravity * dt));
        this.velocityY = Math.min(config.bird.maxVelocity, Math.max(config.bird.minVelocity, nextVelocity));
        this.position.y = f32(this.position.y + f32(this.velocityY * dt));
        this.sync();
    }

    sync(): void {
        const node = (globalThis as any).__flappyScene.GetNodeById(this.nodeId) as any;
        if (!node) {
            return;
        }
        node.Translation = { x: this.position.x, y: this.position.y, z: this.position.z };
        node.RecalcTransform(true);
    }
}
