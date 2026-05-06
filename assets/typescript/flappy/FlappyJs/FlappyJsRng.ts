export class FXorShift32 {
    private state: number;

    constructor(seed = 0x00C0FFEE) {
        this.state = seed >>> 0;
        if (this.state === 0) {
            this.state = 0x00C0FFEE;
        }
    }

    NextU32(): number {
        let x = this.state >>> 0;
        x = (x ^ ((x << 13) >>> 0)) >>> 0;
        x = (x ^ (x >>> 17)) >>> 0;
        x = (x ^ ((x << 5) >>> 0)) >>> 0;
        this.state = x >>> 0;
        return this.state;
    }

    NextFloat01(): number {
        return Math.fround(this.NextU32() * Math.fround(1.0 / 4294967295.0));
    }

    GetState(): number {
        return this.state;
    }

    Reset(seed: number): void {
        this.state = seed >>> 0;
        if (this.state === 0) {
            this.state = 0x00C0FFEE;
        }
    }
}
