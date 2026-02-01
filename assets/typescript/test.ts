import * as NE from "./Engine";

let hasRun = false;

function tryRunTest(): boolean {
    const nodeId = NE.FindNodeIdWithComponent("RenderComponent");
    if (nodeId < 0) {
        return false;
    }

    const nodeName = NE.GetNodeName(nodeId);
    const translation = NE.GetNodeTranslation(nodeId) as NE.Vec3;
    const render = NE.GetComponent(nodeId, "RenderComponent") as NE.RenderComponent;
    if (!render) {
        return false;
    }

    NE.println(`[test.ts] ${nodeName} pos=(${translation.x}, ${translation.y}, ${translation.z}) visible=${render.Visible}`);

    render.Visible = !render.Visible;
    const toggled = render.ToggleVisible();
    NE.println(`[test.ts] ToggleVisible() => ${toggled} visible=${render.Visible}`);
    return true;
}

NE.GetEngine().RegisterJSCallback((_delta: number) => {
    if (hasRun) {
        return;
    }

    if (tryRunTest()) {
        hasRun = true;
    }
});
