import * as NE from "../Engine";
import type { FGameplayConfig, FReplayConfig, Vec3 } from "./FlappyCommon";

const gameplayConfigPath = "assets/configs/flappy/gameplay.json";
const replayConfigPath = "assets/configs/flappy/replay.json";

function vec3FromArray(value: number[]): Vec3 {
    return { x: value[0], y: value[1], z: value[2] };
}

export function LoadGameplayConfig(path = gameplayConfigPath): FGameplayConfig {
    const raw = NE.LoadJson(path);
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
        rngSeed: raw.determinism.rngSeed,
    };
}

export function LoadReplayConfig(path = replayConfigPath): FReplayConfig {
    return NE.LoadJson(path) as FReplayConfig;
}
