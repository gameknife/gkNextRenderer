import * as NE from "./Engine";
import { helperValue } from "./helper";

let hasRun = false;

NE.RegisterLifecycleHooks({
    onInputEvent: (_event: NE.InputEvent) => {
    },
});

function tryRunTest(): boolean {
    const scene = NE.Global.GetScene();
    const nodeId = scene.FindNodeIdWithComponent("RenderComponent");
    if (nodeId < 0) {
        return false;
    }

    const node = scene.GetNodeById(nodeId) as NE.Node;
    const nodeName = node.Name;
    const translation = node.Translation as NE.Vec3;
    const render = node.GetComponent("RenderComponent") as NE.RenderComponent;
    if (!render) {
        return false;
    }

    const config = NE.LoadJson("assets/configs/flappy/gameplay.json");
    const time = NE.Global.GetEngine().GetTime();
    NE.Audio.PlaySfx("assets/sounds/flappy_missing_regression.wav", 0.0);
    NE.Global.spdlog("info", `[test.ts] helper=${helperValue} gravity=${config.bird.gravity} time=${time}`);
    NE.Global.spdlog("info", `[test.ts] ${nodeName} pos=(${translation.x}, ${translation.y}, ${translation.z}) visible=${render.Visible}`);

    render.Visible = !render.Visible;
    const toggled = render.ToggleVisible();
    NE.Global.spdlog("info", `[test.ts] ToggleVisible() => ${toggled} visible=${render.Visible}`);

    return true;
}

NE.RegisterTickCallback((_delta: number) => {
    if (hasRun) {
        return;
    }

    if (tryRunTest()) {
        hasRun = true;
    }
});
