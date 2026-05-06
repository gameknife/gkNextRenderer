import * as NE from "../../Engine";
import { NextGameInstanceBase, RunGameInstance } from "../../NextGameInstanceBase";
import type { EGameState, FFlappyTraceFrame, FGameplayConfig, Vec3 } from "../FlappyCommon";
import { LoadGameplayConfig, LoadReplayConfig } from "../FlappyConfig";
import { FFlappyJsBird } from "./FlappyJsBird";
import { FFlappyJsPipes } from "./FlappyJsPipes";
import { FXorShift32 } from "./FlappyJsRng";

const f32 = Math.fround;
const flapSfx = "assets/sounds/flappy_flap.wav";
const scoreSfx = "assets/sounds/flappy_score.wav";
const hitSfx = "assets/sounds/flappy_hit.wav";

function Vec3(x: number, y: number, z: number): Vec3 {
    return { x, y, z };
}

function CreateNode(name: string,
                    modelId: number,
                    materialId: number,
                    translation: Vec3,
                    scale: Vec3,
                    visible = true): number {
    return NE.SceneBuild.AddRenderNode({
        name,
        modelId,
        materialId,
        translation,
        scale,
        visible,
    });
}

class FlappyJsGameInstance extends NextGameInstanceBase {
    private config: FGameplayConfig = LoadGameplayConfig();
    private rng = new FXorShift32(0x00C0FFEE);
    private bird = new FFlappyJsBird();
    private pipes = new FFlappyJsPipes();
    private state: EGameState = "Ready";
    private score = 0;
    private fixedAccumulator = 0.0;
    private deadTimer = 0.0;
    private pendingFlap = false;
    private sceneReady = false;
    private replayDone = false;

    OnInit(): void {
        this.config = LoadGameplayConfig();
        this.rng.Reset(this.config.rngSeed);
        this.pipes.Reset(this.config.pipe);
        this.ResetRuntime();
    }

    OnTick(deltaSeconds: number): void {
        if (NE.IsReplayMode()) {
            if (this.sceneReady && !this.replayDone) {
                this.RunReplayToCompletion();
            }
            return;
        }

        if (this.state === "Dead") {
            this.deadTimer = f32(this.deadTimer + deltaSeconds);
        }

        this.fixedAccumulator = f32(this.fixedAccumulator + Math.min(deltaSeconds, 0.25));
        while (this.fixedAccumulator >= this.config.fixedDeltaSeconds) {
            const flap = this.pendingFlap;
            this.pendingFlap = false;
            this.FixedStep(flap);
            this.fixedAccumulator = f32(this.fixedAccumulator - this.config.fixedDeltaSeconds);
        }

        if (this.sceneReady) {
            NE.Global.GetScene().MarkTransformDirty();
        }
    }

    OnRenderUI(): boolean {
        const screen = NE.GetScreenSize();
        const drawCenteredText = (text: string, y: number, scale: number, alpha = 1.0): void => {
            const textSize = NE.UI.CalcTextSize(text, scale);
            NE.UI.DrawText(text, Math.max(0, screen.x * 0.5 - textSize.x * 0.5), y, scale, 1.0, 1.0, 1.0, alpha);
        };

        drawCenteredText(`${this.score}`, 28.0, 2.4);
        if (this.state === "Ready") {
            drawCenteredText("Press Space to Start", Math.max(0, screen.y * 0.5 - 12), 1.4, 0.96);
        } else if (this.state === "Dead") {
            drawCenteredText(`Score: ${this.score}\nPress Any Key to Restart`, Math.max(0, screen.y * 0.5 - 24), 1.4, 0.96);
        }
        return false;
    }

    OnInputEvent(event: NE.InputEvent): boolean {
        if (event.type === "keyDown") {
            if (event.repeated) {
                return false;
            }
            if (event.key === "esc") {
                NE.RequestClose();
                return true;
            }
            if (this.state === "Dead" && this.deadTimer >= this.config.deadHitStopSeconds) {
                this.RestartScene();
                return true;
            }
            if (event.key === "space") {
                this.StartOrFlap();
                return true;
            }
            return false;
        }

        if (event.type === "mouseButtonDown") {
            if (event.mouseButton !== 1) {
                return false;
            }
            if (this.state === "Dead" && this.deadTimer >= this.config.deadHitStopSeconds) {
                this.RestartScene();
                return true;
            }
            this.StartOrFlap();
            return true;
        }

        if (event.type === "gamepadButtonDown") {
            if (this.state === "Dead" && this.deadTimer >= this.config.deadHitStopSeconds) {
                this.RestartScene();
                return true;
            }
            if (event.gamepadButton === "south") {
                this.StartOrFlap();
                return true;
            }
        }
        return false;
    }

    BeforeSceneRebuild(): void {
        this.sceneReady = false;
        this.pipes.Reset(this.config.pipe);

        const birdModelId = NE.SceneBuild.AddProceduralModel({
            type: "sphere",
            center: Vec3(0.0, 0.0, 0.0),
            radius: this.config.bird.radius,
        });
        const boxModelId = NE.SceneBuild.AddProceduralModel({
            type: "box",
            min: Vec3(-0.5, -0.5, -0.5),
            max: Vec3(0.5, 0.5, 0.5),
        });
        const backgroundModelId = NE.SceneBuild.AddProceduralModel({
            type: "box",
            min: Vec3(-0.5, -0.5, -0.02),
            max: Vec3(0.5, 0.5, 0.02),
        });

        const birdMaterialId = NE.SceneBuild.AddLambertianMaterial(Vec3(1.0, 0.82, 0.12));
        const pipeMaterialId = NE.SceneBuild.AddLambertianMaterial(Vec3(0.52, 0.58, 0.62));
        const boundaryMaterialId = NE.SceneBuild.AddLambertianMaterial(Vec3(0.30, 0.78, 0.32));
        const backgroundMaterialId = NE.SceneBuild.AddLambertianMaterial(Vec3(0.30, 0.62, 0.94));

        const worldWidth = this.config.world.maxX - this.config.world.minX;
        const birdNodeId = CreateNode("FlappyJs_Bird",
                                      birdModelId,
                                      birdMaterialId,
                                      this.config.bird.initialPosition,
                                      Vec3(1.0, 1.0, 1.0));
        this.bird.SetNode(birdNodeId);
        this.bird.Reset(this.config.bird);

        CreateNode("FlappyJs_Floor",
                   boxModelId,
                   boundaryMaterialId,
                   Vec3(0.0, this.config.world.minY - 0.2, this.config.world.gameplayZ),
                   Vec3(worldWidth + 8.0, 0.4, 0.35));
        CreateNode("FlappyJs_Ceiling",
                   boxModelId,
                   boundaryMaterialId,
                   Vec3(0.0, this.config.world.maxY + 0.2, this.config.world.gameplayZ),
                   Vec3(worldWidth + 8.0, 0.4, 0.35));
        CreateNode("FlappyJs_Background",
                   backgroundModelId,
                   backgroundMaterialId,
                   Vec3(0.0, 0.0, -2.0),
                   Vec3(worldWidth + 8.0, this.config.world.maxY - this.config.world.minY + 2.0, 1.0));

        for (let index = 0; index < Math.max(1, this.config.pipe.poolSize); index += 1) {
            const topNodeId = CreateNode(`FlappyJs_PipeTop_${index}`,
                                         boxModelId,
                                         pipeMaterialId,
                                         Vec3(0.0, -40.0, this.config.world.gameplayZ),
                                         Vec3(this.config.pipe.width, 1.0, 0.35),
                                         false);
            const bottomNodeId = CreateNode(`FlappyJs_PipeBottom_${index}`,
                                            boxModelId,
                                            pipeMaterialId,
                                            Vec3(0.0, -40.0, this.config.world.gameplayZ),
                                            Vec3(this.config.pipe.width, 1.0, 0.35),
                                            false);
            this.pipes.SetNodes(index, topNodeId, bottomNodeId);
        }
    }

    OnSceneLoaded(): void {
        this.sceneReady = true;
        this.ResetRuntime();
    }

    OverrideRenderCamera(): NE.CameraOverride {
        return {
            position: this.config.camera.position,
            target: this.config.camera.target,
            up: this.config.camera.up,
            fov: this.config.camera.fieldOfView,
        };
    }

    private ResetRuntime(): void {
        this.state = "Ready";
        this.score = 0;
        this.fixedAccumulator = 0.0;
        this.deadTimer = 0.0;
        this.pendingFlap = false;
        this.rng.Reset(this.config.rngSeed);
        this.bird.Reset(this.config.bird);
        this.pipes.Reset(this.config.pipe);
    }

    private RestartScene(): void {
        this.ResetRuntime();
        NE.RequestLoadScene("Empty.proc");
    }

    private StartOrFlap(): void {
        if (this.state === "Ready") {
            this.state = "Playing";
            this.pendingFlap = true;
            this.PlaySfx(flapSfx);
            return;
        }

        if (this.state === "Playing") {
            this.pendingFlap = true;
            this.PlaySfx(flapSfx);
        }
    }

    private FixedStep(flapRequested: boolean): void {
        if (this.state === "Ready") {
            if (flapRequested) {
                this.state = "Playing";
                this.bird.Flap(this.config.bird);
            } else {
                this.bird.SyncVisual();
                return;
            }
        }

        if (this.state !== "Playing") {
            return;
        }

        if (flapRequested) {
            this.bird.Flap(this.config.bird);
        }

        this.bird.Update(this.config.fixedDeltaSeconds, this.config.bird);
        this.pipes.Update(this.config.fixedDeltaSeconds, this.config.pipe, this.config.world, this.rng);

        const scored = this.pipes.ConsumeScoreEvents(this.config.bird.initialPosition.x);
        if (scored > 0) {
            this.score += scored;
            this.PlaySfx(scoreSfx);
        }

        const minBirdY = f32(this.config.world.minY + this.config.bird.radius);
        const maxBirdY = f32(this.config.world.maxY - this.config.bird.radius);
        const birdPosition = this.bird.GetPosition();
        if (birdPosition.y < minBirdY || birdPosition.y > maxBirdY ||
            this.pipes.CheckCollision(birdPosition, this.config.bird.radius, this.config.pipe)) {
            this.Die();
        }
    }

    private Die(): void {
        if (this.state === "Dead") {
            return;
        }
        this.state = "Dead";
        this.deadTimer = 0.0;
        this.PlaySfx(hitSfx);
    }

    private PlaySfx(path: string): void {
        NE.Audio.PlaySfx(path);
    }

    private RunReplayToCompletion(): void {
        this.replayDone = true;
        this.ResetRuntime();
        const replayConfig = LoadReplayConfig();
        const flapFrames = new Set<number>(replayConfig.flapFrames);
        const trace: FFlappyTraceFrame[] = [];
        let deathFrame = -1;

        for (let frame = 0; frame < replayConfig.maxFrames; frame += 1) {
            const flap = flapFrames.has(frame);
            this.FixedStep(flap);
            trace.push({
                frame,
                birdY: this.bird.GetPosition().y,
                birdVelocityY: this.bird.GetVelocityY(),
                score: this.score,
                state: this.state,
            });

            if (deathFrame < 0 && this.state === "Dead") {
                deathFrame = frame;
            }
        }

        this.WriteReplayTrace(trace, deathFrame);
        NE.RequestClose();
    }

    private WriteReplayTrace(trace: FFlappyTraceFrame[], deathFrame: number): void {
        NE.WriteFile("out/flappy_js_trace.json", `${JSON.stringify({
            implementation: "FlappyJs",
            fixedDeltaSeconds: f32(this.config.fixedDeltaSeconds),
            rngSeed: this.config.rngSeed,
            deathFrame,
            frames: trace,
        }, null, 2)}\n`);
    }
}

RunGameInstance(new FlappyJsGameInstance());
