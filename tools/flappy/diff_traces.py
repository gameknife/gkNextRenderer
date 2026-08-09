#!/usr/bin/env python3
import argparse
import json
from pathlib import Path


def load_trace(path: Path):
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def format_value(value):
    return str(value)


def compare_traces(cpp, js, max_rows):
    mismatches = []

    def check(path, left, right):
        if left != right:
            mismatches.append((path, left, right))

    check("fixedDeltaSeconds", cpp.get("fixedDeltaSeconds"), js.get("fixedDeltaSeconds"))
    check("rngSeed", cpp.get("rngSeed"), js.get("rngSeed"))
    check("deathFrame", cpp.get("deathFrame"), js.get("deathFrame"))

    cpp_frames = cpp.get("frames", [])
    js_frames = js.get("frames", [])
    check("frames.length", len(cpp_frames), len(js_frames))

    max_bird_y_delta = 0.0
    max_velocity_delta = 0.0
    max_bird_y_frame = None
    max_velocity_frame = None

    for index, (cpp_frame, js_frame) in enumerate(zip(cpp_frames, js_frames)):
        frame_id = cpp_frame.get("frame", index)
        for key in ("frame", "score", "state"):
            if cpp_frame.get(key) != js_frame.get(key):
                mismatches.append((f"frames[{index}].{key}", cpp_frame.get(key), js_frame.get(key)))

        bird_y_delta = abs(cpp_frame.get("birdY", 0.0) - js_frame.get("birdY", 0.0))
        velocity_delta = abs(cpp_frame.get("birdVelocityY", 0.0) - js_frame.get("birdVelocityY", 0.0))
        if bird_y_delta > max_bird_y_delta:
            max_bird_y_delta = bird_y_delta
            max_bird_y_frame = frame_id
        if velocity_delta > max_velocity_delta:
            max_velocity_delta = velocity_delta
            max_velocity_frame = frame_id

        if cpp_frame.get("birdY") != js_frame.get("birdY"):
            mismatches.append((f"frames[{index}].birdY", cpp_frame.get("birdY"), js_frame.get("birdY")))
        if cpp_frame.get("birdVelocityY") != js_frame.get("birdVelocityY"):
            mismatches.append((f"frames[{index}].birdVelocityY", cpp_frame.get("birdVelocityY"), js_frame.get("birdVelocityY")))

    return {
        "mismatches": mismatches[:max_rows],
        "mismatchCount": len(mismatches),
        "maxBirdYDelta": max_bird_y_delta,
        "maxBirdYFrame": max_bird_y_frame,
        "maxVelocityDelta": max_velocity_delta,
        "maxVelocityFrame": max_velocity_frame,
    }


def build_report(cpp_path, js_path, cpp, js, result):
    status = "PASS" if result["mismatchCount"] == 0 else "FAIL"
    lines = [
        "# Flappy Bird Parity Report",
        "",
        f"Status: **{status}**",
        "",
        "## Inputs",
        "",
        f"- C++ trace: `{cpp_path}`",
        f"- JS trace: `{js_path}`",
        f"- C++ implementation: `{cpp.get('implementation')}`",
        f"- JS implementation: `{js.get('implementation')}`",
        "",
        "## Summary",
        "",
        f"- fixedDeltaSeconds: `{format_value(cpp.get('fixedDeltaSeconds'))}`",
        f"- rngSeed: `{cpp.get('rngSeed')}`",
        f"- deathFrame: C++ `{cpp.get('deathFrame')}`, JS `{js.get('deathFrame')}`",
        f"- frame count: C++ `{len(cpp.get('frames', []))}`, JS `{len(js.get('frames', []))}`",
        f"- exact mismatches: `{result['mismatchCount']}`",
        f"- max birdY delta: `{format_value(result['maxBirdYDelta'])}` at frame `{result['maxBirdYFrame']}`",
        f"- max velocity delta: `{format_value(result['maxVelocityDelta'])}` at frame `{result['maxVelocityFrame']}`",
        "",
        "## Reproduce",
        "",
        "```bash",
        "./gnb.sh run FlappyCpp --flappy-replay",
        "./gnb.sh run FlappyJs --flappy-replay",
        "python3 tools/flappy/diff_traces.py",
        "```",
        "",
    ]

    if result["mismatchCount"] > 0:
        lines.extend([
            "## First Mismatches",
            "",
            "| Path | C++ | JS |",
            "|---|---:|---:|",
        ])
        for path, left, right in result["mismatches"]:
            lines.append(f"| `{path}` | `{format_value(left)}` | `{format_value(right)}` |")
        lines.append("")

    lines.extend([
        "## Visual Test Note",
        "",
        "`gkNextVisualTest` currently runs scene files through `RequestLoadScene`; it does not launch separate application targets. Flappy parity is therefore validated with executable-level deterministic replay traces for now.",
        "",
    ])
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description="Compare FlappyCpp and FlappyJs replay traces.")
    parser.add_argument("--cpp", default="out/flappy_cpp_trace.json", help="Path to the C++ trace JSON.")
    parser.add_argument("--js", default="out/flappy_js_trace.json", help="Path to the JS trace JSON.")
    parser.add_argument("--report", help="Optional markdown report output path.")
    parser.add_argument("--max-rows", type=int, default=20, help="Maximum mismatch rows to print/report.")
    args = parser.parse_args()

    cpp_path = Path(args.cpp)
    js_path = Path(args.js)
    cpp = load_trace(cpp_path)
    js = load_trace(js_path)
    result = compare_traces(cpp, js, args.max_rows)
    report = build_report(cpp_path, js_path, cpp, js, result)

    if args.report:
        report_path = Path(args.report)
        report_path.parent.mkdir(parents=True, exist_ok=True)
        report_path.write_text(report + "\n", encoding="utf-8")

    print(report)
    return 0 if result["mismatchCount"] == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
