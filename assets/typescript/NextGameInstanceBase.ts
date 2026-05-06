import * as NE from "./Engine";

export abstract class NextGameInstanceBase {
    OnInit(): void {}
    OnTick(_deltaSeconds: number): void {}
    OnDestroy(): void {}
    BeforeSceneRebuild(): void {}
    OnSceneLoaded(): void {}
    OnRenderUI(): boolean { return false; }
    OnInputEvent(_event: NE.InputEvent): boolean { return false; }
    OverrideRenderCamera(): NE.CameraOverride | undefined { return undefined; }
}

function applyOverrideCamera(instance: NextGameInstanceBase): void {
    const camera = instance.OverrideRenderCamera();
    if (camera) {
        NE.SetOverrideCamera(camera);
    }
}

export function RunGameInstance(instance: NextGameInstanceBase): void {
    NE.RegisterLifecycleHooks({
        onInit: () => {
            instance.OnInit();
            applyOverrideCamera(instance);
        },
        onDestroy: () => instance.OnDestroy(),
        onBeforeSceneRebuild: () => instance.BeforeSceneRebuild(),
        onSceneLoaded: () => instance.OnSceneLoaded(),
        onRenderUI: () => instance.OnRenderUI(),
        onInputEvent: (event: NE.InputEvent) => instance.OnInputEvent(event),
    });

    NE.Global.GetEngine().RegisterJSCallback((deltaSeconds: number) => {
        applyOverrideCamera(instance);
        instance.OnTick(deltaSeconds);
    });
}
