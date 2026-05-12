import type * as NE from "../Engine";

export type Vec3 = NE.Vec3;

export type EGameState = "Ready" | "Playing" | "Dead";

export interface FCameraConfig {
    position: Vec3;
    target: Vec3;
    up: Vec3;
    fieldOfView: number;
}

export interface FWorldConfig {
    minX: number;
    maxX: number;
    minY: number;
    maxY: number;
    gameplayZ: number;
}

export interface FBirdConfig {
    initialPosition: Vec3;
    radius: number;
    gravity: number;
    flapVelocity: number;
    minVelocity: number;
    maxVelocity: number;
}

export interface FPipeConfig {
    width: number;
    gapHeight: number;
    gapCenterMinY: number;
    gapCenterMaxY: number;
    spawnInterval: number;
    spawnX: number;
    destroyX: number;
    speed: number;
    poolSize: number;
}

export interface FGameplayConfig {
    camera: FCameraConfig;
    world: FWorldConfig;
    bird: FBirdConfig;
    pipe: FPipeConfig;
    fixedDeltaSeconds: number;
    deadHitStopSeconds: number;
    rngSeed: number;
}

export interface FReplayConfig {
    maxFrames: number;
    flapFrames: number[];
}

export interface FFlappyTraceFrame {
    frame: number;
    birdY: number;
    birdVelocityY: number;
    score: number;
    state: EGameState;
}
