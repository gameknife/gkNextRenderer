export interface Vec2 { x: number; y: number; }
export interface Vec3 { x: number; y: number; z: number; }
export interface Vec4 { x: number; y: number; z: number; w: number; }
export interface Quat { x: number; y: number; z: number; w: number; }

export class NextEngine {
    GetTotalFrames(): number;
    GetTime(): number;
    GetDeltaSeconds(): number;
    GetSmoothDeltaSeconds(): number;
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
export interface Node {
    RecalcTransform(full?: boolean): void;
}
export class Scene {
    GetIndicesCount(): number;
    FindNodeIdWithComponent(arg0: string): number;
    GetNodeById(arg0: number): any;
}
export interface Scene {
    GetNodeById(nodeId: number): Node;
    AddLambertianMaterial(color: Vec3): number;
    AddDiffuseLightMaterial(color: Vec3, intensity?: number): number;
    AddRenderNode(spec: RenderNodeSpec): number;
    RemoveNodeById(nodeId: number): void;
    MarkTransformDirty(): void;
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

export namespace Input {
    function IsKeyDown(name: string): boolean;
    function IsKeyPressed(name: string): boolean;
    function IsMouseButtonDown(button: number): boolean;
    function IsMouseButtonPressed(button: number): boolean;
    function GetGamepadButton(name: string): boolean;
}

export namespace Audio {
    function PlaySfx(path: string, volume?: number): void;
    function PlayMusic(path: string, volume?: number): void;
    function StopMusic(): void;
}

export namespace UI {
    function Begin(name: string, flags?: number): boolean;
    function End(): void;
    function Text(text: string): void;
    function SetCursorPos(x: number, y: number): void;
    function GetWindowSize(): Vec2;
    function SetWindowFontScale(scale: number): void;
    function GetScreenSize(): Vec2;
    function CalcTextSize(text: string, scale?: number): Vec2;
    function DrawText(text: string, x: number, y: number, scale?: number, r?: number, g?: number, b?: number, a?: number): void;
}

export type ProceduralModelSpec =
    | { type: "box"; min: Vec3; max: Vec3 }
    | { type: "sphere"; center?: Vec3; radius: number };
export interface RenderNodeSpec {
    name: string;
    modelId: number;
    materialId: number;
    translation?: Vec3;
    scale?: Vec3;
    visible?: boolean;
}

export namespace SceneBuild {
    function AddProceduralModel(spec: ProceduralModelSpec): number;
    function AddLambertianMaterial(color: Vec3): number;
    function AddDiffuseLightMaterial(color: Vec3, intensity?: number): number;
    function AddRenderNode(spec: RenderNodeSpec): number;
}
export type InputEventType = "keyDown" | "keyUp" | "mouseButtonDown" | "mouseButtonUp" | "gamepadButtonDown" | "gamepadButtonUp";
export interface InputEvent {
    type: InputEventType;
    key?: string;
    mouseButton?: number;
    gamepadButton?: string;
    repeated?: boolean;
}

export interface LifecycleHooks {
    onInit?: () => void;
    onDestroy?: () => void;
    onBeforeSceneRebuild?: () => void;
    onSceneLoaded?: () => void;
    onRenderUI?: () => boolean | void;
    onInputEvent?: (event: InputEvent) => boolean | void;
}
export interface CameraOverride { position: Vec3; target: Vec3; up: Vec3; fov: number; }
export function RegisterLifecycleHooks(hooks: LifecycleHooks): void;
export function LoadJson(path: string): any;
export function RequestLoadScene(filename: string): void;
export function RequestClose(): void;
export function GetScreenSize(): Vec2;
export function SetOverrideCamera(camera: CameraOverride): void;
export function IsReplayMode(): boolean;
export function WriteFile(path: string, content: string): void;
