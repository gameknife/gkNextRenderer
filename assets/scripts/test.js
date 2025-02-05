import * as NE from "./Engine";
class ScriptComponent extends NE.NextComponent {
    constructor() {
        super();
        this.name_ = "ScriptComponent";
        this.id_ = 1;
    }
    get_info() {
        return this.name_ + " " + this.id_;
    }
}
let testComponent = new ScriptComponent();
NE.println(testComponent.get_info());
NE.println("Hello World from typescript");
let frame = NE.GetEngine().GetTestNumber();
NE.println("Frame: ", frame);
//# sourceMappingURL=test.js.map