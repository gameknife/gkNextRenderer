export class NextEngine {
    GetTotalFrames(): number;
    GetTestNumber(): number;
    RegisterJSCallback(callback: (param: number) => void): void;
    GetScenePtr(): Scene;
}

export class NextComponent {
    name_ : string
    id_ : number
}

export class Scene {
    GetIndicesCount(): number;
}


export function println(...args: any[]): void;
export function GetEngine(): NextEngine;