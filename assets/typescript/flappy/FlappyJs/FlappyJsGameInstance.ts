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

interface EnvironmentComponent {
    HasSky: boolean;
    SkyIdx: number;
    SkyIntensity: number;
    HasSun: boolean;
    SunIntensity: number;
    SunRotation: number;
    SunElevation: number;
}

class FlappyJsGameInstance extends NextGameInstanceBase {
    private config: FGameplayConfig = LoadGameplayConfig();
    private rng = new FXorShift32(0x00C0FFEE);
    private bird = new FFlappyJsBird();
    private pipes = new FFlappyJsPipes();
    private mountainNodeIds: number[] = [];
    private vegetationNodeIds: number[] = [];
    private cloudNodeIds: number[] = [];
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
        const centerX = screen.x * 0.5;
        const centerY = screen.y * 0.5;
        const drawCenteredText = (text: string, y: number, scale: number, r = 1.0, g = 1.0, b = 1.0, a = 1.0): void => {
            const textSize = NE.UI.CalcTextSize(text, scale);
            const x = Math.max(0, centerX - textSize.x * 0.5);
            NE.UI.DrawText(text, x + 2.0, y + 2.0, scale, 0.08, 0.11, 0.14, a * 0.45);
            NE.UI.DrawText(text, x, y, scale, r, g, b, a);
        };
        const drawPanel = (width: number, height: number, y: number): { x: number; y: number } => {
            const x = centerX - width * 0.5;
            NE.UI.DrawRectFilled(x + 6.0, y + 8.0, width, height, 0.06, 0.11, 0.15, 0.32, 18.0);
            NE.UI.DrawRectFilled(x, y, width, height, 0.07, 0.13, 0.17, 0.82, 18.0);
            NE.UI.DrawRect(x, y, width, height, 1.0, 1.0, 1.0, 0.18, 18.0, 1.5);
            return { x, y };
        };

        const scoreWidth = 136.0 + Math.max(0, Math.floor(this.score / 10)) * 18.0;
        const scoreX = centerX - scoreWidth * 0.5;
        NE.UI.DrawRectFilled(scoreX + 3.0, 27.0, scoreWidth, 56.0, 0.04, 0.09, 0.13, 0.32, 16.0);
        NE.UI.DrawRectFilled(scoreX, 24.0, scoreWidth, 56.0, 0.09, 0.16, 0.20, 0.82, 16.0);
        NE.UI.DrawRect(scoreX, 24.0, scoreWidth, 56.0, 1.0, 1.0, 1.0, 0.16, 16.0, 1.0);
        drawCenteredText(`${this.score}`, 31.0, 2.05);

        if (this.state === "Ready") {
            const panel = drawPanel(Math.max(280.0, Math.min(520.0, screen.x - 48.0)), 178.0, centerY - 96.0);
            drawCenteredText("FLAPPY", panel.y + 26.0, 2.0, 1.0, 0.90, 0.40);
            drawCenteredText("Thread the gap. Keep the rhythm.", panel.y + 76.0, 1.0, 0.83, 0.91, 0.93, 0.92);
            drawCenteredText("SPACE / CLICK / GAMEPAD A", panel.y + 121.0, 1.1, 0.49, 0.90, 0.66, 1.0);
        } else if (this.state === "Dead") {
            const panel = drawPanel(Math.max(280.0, Math.min(500.0, screen.x - 48.0)), 166.0, centerY - 88.0);
            drawCenteredText("GAME OVER", panel.y + 24.0, 1.75, 1.0, 0.53, 0.48);
            drawCenteredText(`Score ${this.score}`, panel.y + 73.0, 1.25, 1.0, 1.0, 1.0, 0.96);
            drawCenteredText("PRESS ANY KEY TO RESTART", panel.y + 116.0, 1.0, 0.49, 0.90, 0.66, 0.96);
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
        const mountainMaterialId = NE.SceneBuild.AddLambertianMaterial(Vec3(0.38, 0.42, 0.46));
        const vegetationMaterialId = NE.SceneBuild.AddLambertianMaterial(Vec3(0.24, 0.68, 0.22));
        const cloudMaterialId = NE.SceneBuild.AddLambertianMaterial(Vec3(0.92, 0.94, 0.96));

        const worldWidth = this.config.world.maxX - this.config.world.minX;
        const gameplayDepth = this.config.camera.position.z - this.config.world.gameplayZ;
        const backdropDepth = this.config.camera.position.z - this.config.world.backdropZ;
        const backdropScale = gameplayDepth > 0.0 ? Math.max(1.0, backdropDepth / gameplayDepth) : 1.0;
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
                   Vec3(0.0, 0.0, this.config.world.backdropZ),
                   Vec3((worldWidth + 8.0) * backdropScale,
                        (this.config.world.maxY - this.config.world.minY + 2.0) * backdropScale,
                        1.0));

        this.mountainNodeIds = [];
        const mountainCount = Math.max(1, this.config.parallax.mountainCount);
        for (let index = 0; index < mountainCount; index += 1) {
            const x = (index - (mountainCount - 1) * 0.5) * this.config.parallax.mountainSpacing;
            const width = 7.0 + (index % 3) * 1.0;
            const height = 10.0 + ((index + 1) % 3) * 1.1;
            const nodeId = CreateNode(`FlappyJs_Mountain_${index}`,
                                      backgroundModelId,
                                      mountainMaterialId,
                                      Vec3(x, -10.5, this.config.parallax.mountainZ),
                                      Vec3(width, height, 1.0));
            this.mountainNodeIds.push(nodeId);
        }

        this.vegetationNodeIds = [];
        const vegetationCount = Math.max(1, this.config.parallax.vegetationCount);
        for (let index = 0; index < vegetationCount; index += 1) {
            const x = (index - (vegetationCount - 1) * 0.5) * this.config.parallax.vegetationSpacing;
            const width = 2.7 + (index % 3) * 0.35;
            const height = 5.6 + ((index + 2) % 4) * 0.35;
            this.vegetationNodeIds.push(CreateNode(`FlappyJs_Vegetation_${index}`,
                                                   backgroundModelId,
                                                   vegetationMaterialId,
                                                   Vec3(x, -8.0, this.config.parallax.vegetationZ),
                                                   Vec3(width, height, 1.0)));
        }

        this.cloudNodeIds = [];
        const cloudCount = Math.max(1, this.config.parallax.cloudCount);
        for (let index = 0; index < cloudCount; index += 1) {
            const x = (index - (cloudCount - 1) * 0.5) * this.config.parallax.cloudSpacing;
            const y = 5.5 + (index % 3) * 2.4;
            const width = 7.0 + ((index + 1) % 3) * 1.2;
            const height = 1.3 + (index % 2) * 0.35;
            this.cloudNodeIds.push(CreateNode(`FlappyJs_Cloud_${index}`,
                                              backgroundModelId,
                                              cloudMaterialId,
                                              Vec3(x, y, this.config.parallax.cloudZ),
                                              Vec3(width, height, 1.0)));
        }

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
        const scene = NE.Global.GetScene();
        const environmentNodeId = scene.FindNodeIdWithComponent("EnvironmentComponent");
        if (environmentNodeId >= 0) {
            const environmentNode = scene.GetNodeById(environmentNodeId);
            const environment = environmentNode.GetComponent("EnvironmentComponent") as EnvironmentComponent;
            environment.HasSky = true;
            environment.SkyIdx = this.config.environment.skyIndex;
            environment.SkyIntensity = this.config.environment.skyIntensity;
            environment.HasSun = true;
            environment.SunIntensity = this.config.environment.sunIntensity;
            environment.SunRotation = this.config.environment.sunRotation;
            environment.SunElevation = this.config.environment.sunElevation;
        }

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
        this.UpdateParallax(this.config.fixedDeltaSeconds);

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

    private UpdateParallax(deltaSeconds: number): void {
        const updateLayer = (nodeIds: number[], speed: number, spacing: number): void => {
            const wrapWidth = nodeIds.length * spacing;
            const wrapMinX = -0.5 * wrapWidth;
            for (const nodeId of nodeIds) {
                const node = NE.Global.GetScene().GetNodeById(nodeId);
                let x = node.Translation.x - speed * deltaSeconds;
                if (x < wrapMinX) {
                    x += wrapWidth;
                }
                node.Translation = { x, y: node.Translation.y, z: node.Translation.z };
            }
        };

        updateLayer(this.mountainNodeIds,
                    this.config.parallax.mountainSpeed,
                    this.config.parallax.mountainSpacing);
        updateLayer(this.vegetationNodeIds,
                    this.config.parallax.vegetationSpeed,
                    this.config.parallax.vegetationSpacing);
        updateLayer(this.cloudNodeIds, this.config.parallax.cloudSpeed, this.config.parallax.cloudSpacing);
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
