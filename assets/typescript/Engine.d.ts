export interface Vec2 { x: number; y: number; }
export interface Vec3 { x: number; y: number; z: number; }
export interface Vec4 { x: number; y: number; z: number; w: number; }
export interface Quat { x: number; y: number; z: number; w: number; }

export class NextEngine {
    GetTotalFrames(): number;
    RegisterJSCallback(arg0: any): void;
}
export class Node {
    readonly Name: string;
    readonly InstanceId: number;
    Translation: Vec3;
    Rotation: Quat;
    Scale: Vec3;
    GetName(): string;
    GetInstanceId(): number;
    GetComponent(arg0: string): any;
}
export class Scene {
    GetIndicesCount(): number;
    FindNodeIdWithComponent(arg0: string): number;
    GetNodeById(arg0: number): any;
}
export class RenderComponent {
    Visible: boolean;
    RayCastVisible: boolean;
    readonly ModelId: number;
    readonly SkinIndex: number;
    Materials: number[];
    ToggleVisible(): boolean;
    ToggleRayCastVisible(): boolean;
}
export class PhysicsComponent {
    Mobility: string;
    PhysicsOffset: Vec3;
}
export class SkinnedMeshComponent {
    PlaySpeed: number;
    readonly IsPlaying: boolean;
    readonly CurrentAnimation: string;
    PlayAnimation(arg0: string, arg1: boolean): void;
    StopAnimation(): void;
    GetAnimationNames(): string[];
}
export type ENodeMobility = "Static" | "Dynamic" | "Kinematic";

export namespace Global {
    function spdlog(level: string, ...args: any[]): void;
    function GetEngine(): NextEngine;
    function GetScene(): Scene;
}
