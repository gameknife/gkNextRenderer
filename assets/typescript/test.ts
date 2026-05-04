import * as NE from "./Engine";

let hasRun = false;

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

    NE.Global.spdlog("info", `[test.ts] ${nodeName} pos=(${translation.x}, ${translation.y}, ${translation.z}) visible=${render.Visible}`);

    render.Visible = !render.Visible;
    const toggled = render.ToggleVisible();
    NE.Global.spdlog("info", `[test.ts] ToggleVisible() => ${toggled} visible=${render.Visible}`);

    return true;
}

NE.Global.GetEngine().RegisterJSCallback((_delta: number) => {
    if (hasRun) {
        return;
    }

    if (tryRunTest()) {
        hasRun = true;
    }
});
