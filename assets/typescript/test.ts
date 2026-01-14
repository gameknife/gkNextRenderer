import * as NE from "./Engine";
class ScriptComponent extends NE.NextComponent {
    constructor() {
        super();
        this.name_ = "ScriptComponent";
        this.id_ = 1;
    }
    get_info() {
        let frame = NE.GetEngine().GetTestNumber();
        return `[test.ts] ${this.name_} ${this.id_} at ${frame}`;
    }
}

let testComponent = new ScriptComponent();
NE.println(testComponent.get_info());
//NE.println("Hello World from typescript");
//NE.println("Frame: ", frame);