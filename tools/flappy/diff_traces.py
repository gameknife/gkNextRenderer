#!/usr/bin/env python3
"""Compare the FlappyCpp replay trace against the C# implementation's trace.

C++ is the behavioural baseline; the C# side exists to prove the scripting layer's bindings are
correct. What that regression actually tests is call count, call order and lifecycle timing — a
binding that fires twice, never, or in the wrong frame moves the score or the death frame, and
those are compared exactly.

Float positions are compared with a tolerance instead. The JS-era implementation had to emulate
C++ float rounding with Math.fround on every expression to keep bit-equality; C# has real float,
so the two sides agree to within accumulated rounding without that ceremony. Demanding
bit-equality here would buy nothing and would make the test fragile against harmless codegen
differences between the two backends.
"""
import argparse
import json
import struct
from pathlib import Path

# Thresholds from docs/designs/dotnet-scripting-design.md section 9.
POSITION_TOLERANCE = 1e-3
VELOCITY_TOLERANCE = 1e-3

# Compared with ==; a difference in any of these means the simulation diverged, not that a float
# rounded differently.
EXACT_FRAME_KEYS = ("frame", "score", "state")


def load_trace(path: Path):
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def format_value(value):
    return str(value)


def as_float32(value):
    """Round a JSON number to the float32 it represents on both sides of the comparison.

    Both implementations store this data as float32; they differ only in how many digits their
    JSON writer emits. nlohmann promotes to double and prints 0.01666666753590107, while
    Utf8JsonWriter prints the shortest text that round-trips as float, 0.016666668. Comparing the
    decoded doubles would report a difference that does not exist in the running program.
    """
    if value is None:
        return None
    return struct.unpack("f", struct.pack("f", float(value)))[0]


def compare_traces(cpp, other, max_rows):
    mismatches = []

    def check(path, left, right):
        if left != right:
            mismatches.append((path, left, right))

    check("fixedDeltaSeconds", as_float32(cpp.get("fixedDeltaSeconds")), as_float32(other.get("fixedDeltaSeconds")))
    check("rngSeed", cpp.get("rngSeed"), other.get("rngSeed"))
    check("deathFrame", cpp.get("deathFrame"), other.get("deathFrame"))

    cpp_frames = cpp.get("frames", [])
    other_frames = other.get("frames", [])
    check("frames.length", len(cpp_frames), len(other_frames))

    max_bird_y_delta = 0.0
    max_velocity_delta = 0.0
    max_bird_y_frame = None
    max_velocity_frame = None

    for index, (cpp_frame, other_frame) in enumerate(zip(cpp_frames, other_frames)):
        frame_id = cpp_frame.get("frame", index)
        for key in EXACT_FRAME_KEYS:
            if cpp_frame.get(key) != other_frame.get(key):
                mismatches.append((f"frames[{index}].{key}", cpp_frame.get(key), other_frame.get(key)))

        bird_y_delta = abs(cpp_frame.get("birdY", 0.0) - other_frame.get("birdY", 0.0))
        velocity_delta = abs(cpp_frame.get("birdVelocityY", 0.0) - other_frame.get("birdVelocityY", 0.0))
        if bird_y_delta > max_bird_y_delta:
            max_bird_y_delta = bird_y_delta
            max_bird_y_frame = frame_id
        if velocity_delta > max_velocity_delta:
            max_velocity_delta = velocity_delta
            max_velocity_frame = frame_id

        if bird_y_delta > POSITION_TOLERANCE:
            mismatches.append((f"frames[{index}].birdY", cpp_frame.get("birdY"), other_frame.get("birdY")))
        if velocity_delta > VELOCITY_TOLERANCE:
            mismatches.append((f"frames[{index}].birdVelocityY",
                               cpp_frame.get("birdVelocityY"), other_frame.get("birdVelocityY")))

    return {
        "mismatches": mismatches[:max_rows],
        "mismatchCount": len(mismatches),
        "maxBirdYDelta": max_bird_y_delta,
        "maxBirdYFrame": max_bird_y_frame,
        "maxVelocityDelta": max_velocity_delta,
        "maxVelocityFrame": max_velocity_frame,
    }


def build_report(cpp_path, other_path, cpp, other, result):
    status = "PASS" if result["mismatchCount"] == 0 else "FAIL"
    lines = [
        "# Flappy Parity Report",
        "",
        f"Status: **{status}**",
        "",
        "## Inputs",
        "",
        f"- C++ trace: `{cpp_path}`",
        f"- scripted trace: `{other_path}`",
        f"- C++ implementation: `{cpp.get('implementation')}`",
        f"- scripted implementation: `{other.get('implementation')}`",
        "",
        "## Thresholds",
        "",
        f"- `{'`, `'.join(EXACT_FRAME_KEYS)}`, `deathFrame`, frame count: exact",
        f"- `birdY`: {POSITION_TOLERANCE}",
        f"- `birdVelocityY`: {VELOCITY_TOLERANCE}",
        "",
        "## Summary",
        "",
        f"- fixedDeltaSeconds: `{format_value(cpp.get('fixedDeltaSeconds'))}`",
        f"- rngSeed: `{cpp.get('rngSeed')}`",
        f"- deathFrame: C++ `{cpp.get('deathFrame')}`, scripted `{other.get('deathFrame')}`",
        f"- frame count: C++ `{len(cpp.get('frames', []))}`, scripted `{len(other.get('frames', []))}`",
        f"- violations: `{result['mismatchCount']}`",
        f"- max birdY delta: `{format_value(result['maxBirdYDelta'])}` at frame `{result['maxBirdYFrame']}`",
        f"- max velocity delta: `{format_value(result['maxVelocityDelta'])}` at frame `{result['maxVelocityFrame']}`",
        "",
        "## Reproduce",
        "",
        "```bash",
        "./gnb.sh run FlappyCpp --flappy-replay",
        "./gnb.sh run FlappyCSharp --flappy-replay",
        "python3 tools/flappy/diff_traces.py",
        "```",
        "",
        "Run the C# side once per backend (`gnb dotnet ci` builds both) — the two backends must",
        "produce the same trace, which is the point of the two-backend design.",
        "",
    ]

    if result["mismatchCount"] > 0:
        lines.extend([
            "## First Violations",
            "",
            "| Path | C++ | scripted |",
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
    parser = argparse.ArgumentParser(
        description="Compare the FlappyCpp replay trace against the FlappyCSharp trace.")
    parser.add_argument("--cpp", default="out/flappy_cpp_trace.json", help="Path to the C++ trace JSON.")
    parser.add_argument("--cs", default="out/flappy_cs_trace.json", help="Path to the C# trace JSON.")
    parser.add_argument("--report", help="Optional markdown report output path.")
    parser.add_argument("--max-rows", type=int, default=20, help="Maximum violation rows to print/report.")
    args = parser.parse_args()

    cpp_path = Path(args.cpp)
    other_path = Path(args.cs)
    cpp = load_trace(cpp_path)
    other = load_trace(other_path)
    result = compare_traces(cpp, other, args.max_rows)
    report = build_report(cpp_path, other_path, cpp, other, result)

    if args.report:
        report_path = Path(args.report)
        report_path.parent.mkdir(parents=True, exist_ok=True)
        report_path.write_text(report + "\n", encoding="utf-8")

    print(report)
    return 0 if result["mismatchCount"] == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
