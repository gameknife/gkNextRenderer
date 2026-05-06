import * as NE from "../../Engine";
import type { FBirdConfig, Vec3 } from "../FlappyCommon";

const f32 = Math.fround;

export class FFlappyJsBird {
    private position: Vec3 = { x: -3.0, y: 0.0, z: 0.0 };
    private velocityY = 0.0;
    private nodeId = 0;

    Reset(config: FBirdConfig): void {
        this.position = { ...config.initialPosition };
        this.velocityY = 0.0;
        this.SyncVisual();
    }

    Flap(config: FBirdConfig): void {
        this.velocityY = f32(config.flapVelocity);
    }

    Update(fixedDeltaSeconds: number, config: FBirdConfig): void {
        const nextVelocity = f32(this.velocityY + f32(config.gravity * fixedDeltaSeconds));
        this.velocityY = Math.min(config.maxVelocity, Math.max(config.minVelocity, nextVelocity));
        this.position.y = f32(this.position.y + f32(this.velocityY * fixedDeltaSeconds));
        this.SyncVisual();
    }

    SyncVisual(): void {
        const node = NE.Global.GetScene().GetNodeById(this.nodeId);
        if (!node) {
            return;
        }

        node.Translation = { x: this.position.x, y: this.position.y, z: this.position.z };
        node.RecalcTransform(true);
    }

    GetPosition(): Vec3 {
        return this.position;
    }

    GetVelocityY(): number {
        return this.velocityY;
    }

    SetNode(nodeId: number): void {
        this.nodeId = nodeId;
    }
}
