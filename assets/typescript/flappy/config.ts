import * as NE from "../Engine";

export interface Vec3 {
    x: number;
    y: number;
    z: number;
}

export interface GameplayConfig {
    camera: {
        position: Vec3;
        target: Vec3;
        up: Vec3;
        fieldOfView: number;
    };
    world: {
        minX: number;
        maxX: number;
        minY: number;
        maxY: number;
        gameplayZ: number;
    };
    bird: {
        initialPosition: Vec3;
        radius: number;
        gravity: number;
        flapVelocity: number;
        minVelocity: number;
        maxVelocity: number;
    };
    pipe: {
        width: number;
        gapHeight: number;
        gapCenterMinY: number;
        gapCenterMaxY: number;
        spawnInterval: number;
        spawnX: number;
        destroyX: number;
        speed: number;
        poolSize: number;
    };
    fixedDeltaSeconds: number;
    deadHitStopSeconds: number;
    determinism: {
        rngSeed: number;
    };
}

export interface ReplayConfig {
    maxFrames: number;
    flapFrames: number[];
}

function vec3FromArray(value: number[]): Vec3 {
    return { x: value[0], y: value[1], z: value[2] };
}

export function loadGameplayConfig(): GameplayConfig {
    const raw = NE.LoadJson("assets/configs/flappy/gameplay.json");
    return {
        camera: {
            position: vec3FromArray(raw.camera.position),
            target: vec3FromArray(raw.camera.target),
            up: vec3FromArray(raw.camera.up),
            fieldOfView: raw.camera.fieldOfView,
        },
        world: raw.world,
        bird: {
            ...raw.bird,
            initialPosition: vec3FromArray(raw.bird.initialPosition),
        },
        pipe: raw.pipe,
        fixedDeltaSeconds: raw.fixedDeltaSeconds,
        deadHitStopSeconds: raw.deadHitStopSeconds,
        determinism: raw.determinism,
    };
}

export function loadReplayConfig(): ReplayConfig {
    return NE.LoadJson("assets/configs/flappy/replay.json") as ReplayConfig;
}
