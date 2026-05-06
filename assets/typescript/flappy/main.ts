import * as NE from "../Engine";
import { Bird } from "./bird";
import { loadGameplayConfig, loadReplayConfig, type GameplayConfig } from "./config";
import { Pipes } from "./pipes";
import { XorShift32 } from "./rng";

const f32 = Math.fround;
const flapSfx = "assets/sounds/flappy_flap.wav";
const scoreSfx = "assets/sounds/flappy_score.wav";
const hitSfx = "assets/sounds/flappy_hit.wav";

type GameState = "Ready" | "Playing" | "Dead";

interface TraceFrame {
    frame: number;
    birdY: number;
    birdVelocityY: number;
    score: number;
    state: GameState;
}

class FlappyGame {
    private config: GameplayConfig = loadGameplayConfig();
    private rng = new XorShift32(this.config.determinism.rngSeed);
    private bird = new Bird();
    private pipes = new Pipes();
    private state: GameState = "Ready";
    private score = 0;
    private accumulator = 0.0;
    private deadTimer = 0.0;
    private sceneReady = false;
    private replayDone = false;
    private pendingFlap = false;

    onInit(): void {
        NE.SetOverrideCamera({
            position: this.config.camera.position,
            target: this.config.camera.target,
            up: this.config.camera.up,
            fov: this.config.camera.fieldOfView,
        });
    }

    onSceneLoaded(): void {
        const scene = NE.Global.GetScene();
        (globalThis as any).__flappyScene = scene;
        this.pipes = new Pipes();

        this.bird.nodeId = scene.AddSphereNode("FlappyJs_Bird", this.config.bird.initialPosition.x, this.config.bird.initialPosition.y, this.config.bird.initialPosition.z, this.config.bird.radius, 1.0, 0.82, 0.12);
        const width = this.config.world.maxX - this.config.world.minX + 8.0;
        scene.AddBoxNode("FlappyJs_Floor", -width * 0.5, this.config.world.minY - 0.4, -0.175, width * 0.5, this.config.world.minY, 0.175, 0.30, 0.78, 0.32);
        scene.AddBoxNode("FlappyJs_Ceiling", -width * 0.5, this.config.world.maxY, -0.175, width * 0.5, this.config.world.maxY + 0.4, 0.175, 0.30, 0.78, 0.32);
        scene.AddBoxNode("FlappyJs_Background", -width * 0.5, this.config.world.minY - 1.0, -2.02, width * 0.5, this.config.world.maxY + 1.0, -1.98, 0.30, 0.62, 0.94);
        for (let index = 0; index < this.config.pipe.poolSize; index += 1) {
            const top = scene.AddBoxNode(`FlappyJs_PipeTop_${index}`, -0.5, -40.5, -0.175, 0.5, -39.5, 0.175, 0.52, 0.58, 0.62);
            const bottom = scene.AddBoxNode(`FlappyJs_PipeBottom_${index}`, -0.5, -40.5, -0.175, 0.5, -39.5, 0.175, 0.52, 0.58, 0.62);
            this.pipes.addPipePair(top, bottom);
        }
        this.sceneReady = true;
        this.reset();
    }

    tick(deltaSeconds: number): void {
        if (!this.sceneReady) {
            return;
        }
        if (NE.IsReplayMode()) {
            this.runReplay();
            return;
        }

        if (this.state === "Dead") {
            this.deadTimer = f32(this.deadTimer + deltaSeconds);
        }

        this.accumulator = f32(this.accumulator + Math.min(deltaSeconds, 0.25));
        while (this.accumulator >= this.config.fixedDeltaSeconds) {
            const flap = this.pendingFlap;
            this.pendingFlap = false;
            this.fixedStep(flap);
            this.accumulator = f32(this.accumulator - this.config.fixedDeltaSeconds);
        }
        NE.Global.GetScene().MarkTransformDirty();
    }

    onInputEvent(event: NE.InputEvent): void {
        if (event.type === "keyDown") {
            if (event.repeated) {
                return;
            }
            if (event.key === "esc") {
                NE.RequestClose();
                return;
            }
            if (this.state === "Dead" && this.deadTimer >= this.config.deadHitStopSeconds) {
                this.restartScene();
                return;
            }
            if (event.key === "space") {
                this.startOrFlap();
            }
            return;
        }

        if (event.type === "mouseButtonDown") {
            if (event.mouseButton !== 1) {
                return;
            }
            if (this.state === "Dead" && this.deadTimer >= this.config.deadHitStopSeconds) {
                this.restartScene();
                return;
            }
            this.startOrFlap();
            return;
        }

        if (event.type === "gamepadButtonDown") {
            if (this.state === "Dead" && this.deadTimer >= this.config.deadHitStopSeconds) {
                this.restartScene();
                return;
            }
            if (event.gamepadButton === "south") {
                this.startOrFlap();
            }
        }
    }

    onRenderUI(): void {
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
    }

    private reset(): void {
        this.state = "Ready";
        this.score = 0;
        this.accumulator = 0.0;
        this.deadTimer = 0.0;
        this.pendingFlap = false;
        this.rng.reset(this.config.determinism.rngSeed);
        this.bird.reset(this.config);
        this.pipes.reset(this.config);
    }

    private restartScene(): void {
        this.reset();
        NE.RequestLoadScene("Empty.proc");
    }

    private startOrFlap(): void {
        if (this.state === "Ready") {
            this.state = "Playing";
            this.pendingFlap = true;
            NE.Audio.PlaySfx(flapSfx);
            return;
        }

        if (this.state === "Playing") {
            this.pendingFlap = true;
            NE.Audio.PlaySfx(flapSfx);
        }
    }

    private fixedStep(flap: boolean): void {
        if (this.state === "Ready") {
            if (flap) {
                this.state = "Playing";
                this.bird.flap(this.config);
            } else {
                this.bird.sync();
                return;
            }
        }
        if (this.state !== "Playing") {
            return;
        }
        if (flap) {
            this.bird.flap(this.config);
        }
        this.bird.update(this.config.fixedDeltaSeconds, this.config);
        this.pipes.update(this.config.fixedDeltaSeconds, this.config, this.rng);
        const scored = this.pipes.consumeScoreEvents(this.config.bird.initialPosition.x);
        if (scored > 0) {
            this.score += scored;
            NE.Audio.PlaySfx(scoreSfx);
        }
        const minBirdY = f32(this.config.world.minY + this.config.bird.radius);
        const maxBirdY = f32(this.config.world.maxY - this.config.bird.radius);
        if (this.bird.position.y < minBirdY || this.bird.position.y > maxBirdY ||
            this.pipes.checkCollision(this.bird.position.x, this.bird.position.y, this.config.bird.radius, this.config)) {
            this.state = "Dead";
            this.deadTimer = 0.0;
            NE.Audio.PlaySfx(hitSfx);
        }
    }

    private runReplay(): void {
        if (this.replayDone) {
            return;
        }
        this.replayDone = true;
        this.reset();
        const replay = loadReplayConfig();
        const flapFrames = new Set<number>(replay.flapFrames);
        const frames: TraceFrame[] = [];
        let deathFrame = -1;
        for (let frame = 0; frame < replay.maxFrames; frame += 1) {
            this.fixedStep(flapFrames.has(frame));
            frames.push({
                frame,
                birdY: this.bird.position.y,
                birdVelocityY: this.bird.velocityY,
                score: this.score,
                state: this.state,
            });
            if (deathFrame < 0 && this.state === "Dead") {
                deathFrame = frame;
            }
        }
        NE.WriteFile("out/flappy_js_trace.json", `${JSON.stringify({
            implementation: "FlappyJs",
            fixedDeltaSeconds: f32(this.config.fixedDeltaSeconds),
            rngSeed: this.config.determinism.rngSeed,
            deathFrame,
            frames,
        }, null, 2)}\n`);
        NE.RequestClose();
    }
}

const game = new FlappyGame();
NE.RegisterLifecycleHooks({
    onInit: () => game.onInit(),
    onSceneLoaded: () => game.onSceneLoaded(),
    onRenderUI: () => game.onRenderUI(),
    onInputEvent: (event: NE.InputEvent) => game.onInputEvent(event),
});
NE.Global.GetEngine().RegisterJSCallback((deltaSeconds: number) => game.tick(deltaSeconds));
