export class XorShift32 {
    private state: number;

    constructor(seed: number) {
        this.state = seed >>> 0;
        if (this.state === 0) {
            this.state = 0x00C0FFEE;
        }
    }

    reset(seed: number): void {
        this.state = seed >>> 0;
        if (this.state === 0) {
            this.state = 0x00C0FFEE;
        }
    }

    nextU32(): number {
        let x = this.state >>> 0;
        x = (x ^ ((x << 13) >>> 0)) >>> 0;
        x = (x ^ (x >>> 17)) >>> 0;
        x = (x ^ ((x << 5) >>> 0)) >>> 0;
        this.state = x >>> 0;
        return this.state;
    }

    nextFloat01(): number {
        return Math.fround(this.nextU32() * Math.fround(1.0 / 4294967295.0));
    }
}
