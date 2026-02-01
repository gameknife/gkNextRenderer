export interface Vec2 { x: number; y: number; }
export interface Vec3 { x: number; y: number; z: number; }
export interface Vec4 { x: number; y: number; z: number; w: number; }
export interface Quat { x: number; y: number; z: number; w: number; }

export class NextEngine {
    GetTotalFrames(): number;
    GetTestNumber(): number;
    RegisterJSCallback(callback: (param: number) => void): void;
    GetScenePtr(): Scene;
}

export class NextComponent {
    name_: string;
    id_: number;
}

export class Scene {
    GetIndicesCount(): number;
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

export function println(...args: any[]): void;
export function GetEngine(): NextEngine;
export function FindNodeIdWithComponent(componentType: string): number;
export function GetNodeName(nodeId: number): string;
export function GetNodeTranslation(nodeId: number): Vec3;
export function GetComponent(nodeId: number, componentType: string): any;
export function GetComponentProperty(nodeId: number, componentType: string, propertyName: string): any;
export function SetComponentProperty(nodeId: number, componentType: string, propertyName: string, value: any): boolean;
export function CallComponentFunction(nodeId: number, componentType: string, functionName: string, ...args: any[]): any;
