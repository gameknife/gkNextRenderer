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
    backdropZ: number;
}

export interface FEnvironmentConfig {
    skyIndex: number;
    skyIntensity: number;
    sunIntensity: number;
    sunRotation: number;
    sunElevation: number;
}

export interface FParallaxConfig {
    mountainZ: number;
    mountainSpeed: number;
    mountainSpacing: number;
    mountainCount: number;
    vegetationZ: number;
    vegetationSpeed: number;
    vegetationSpacing: number;
    vegetationCount: number;
    cloudZ: number;
    cloudSpeed: number;
    cloudSpacing: number;
    cloudCount: number;
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
    environment: FEnvironmentConfig;
    parallax: FParallaxConfig;
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
