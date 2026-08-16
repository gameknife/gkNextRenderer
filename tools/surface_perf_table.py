"""Render a GPU-timer comparison table from a surface-path agent-validation report.

The Visibility Surface plan requires every milestone journal to carry a per-pass GPU comparison,
so the numbers have to come out of the same scripted run every time rather than from a screenshot
of a profiler overlay. Usage:

    python3 tools/surface_perf_table.py out/build/windows/agent_reports/surface-noambient-perf-1080p.json
"""

import io
import json
import sys

TIMERS = ["shadingpass", "gtao pass", "simplecompose pass", "checkerboard resolve", "surface build"]


def blocks(report):
    """Group the report's assert results under the log line that introduced them."""
    label = None
    values = []
    for step in report["steps"]:
        if step["type"] == "log":
            if label is not None:
                yield label, values
            label = step.get("message", "").strip("= ").strip()
            values = []
        elif step["type"] == "assert" and "actual" in step:
            values.append(float(step["actual"]))
    if label is not None:
        yield label, values


def main(path):
    report = json.load(io.open(path, encoding="utf-8"))
    header = ["config"] + TIMERS + ["pass total", "fps"]
    rows = []
    for label, values in blocks(report):
        if len(values) < len(TIMERS) + 1:
            continue
        timings = values[: len(TIMERS)]
        rows.append([label] + ["%.3f" % v for v in timings] +
                    ["%.3f" % sum(timings), "%.0f" % values[len(TIMERS)]])

    widths = [max(len(r[i]) for r in [header] + rows) for i in range(len(header))]
    def line(cells):
        return "| " + " | ".join(c.ljust(widths[i]) for i, c in enumerate(cells)) + " |"
    print("passed:", report.get("passed"))
    print(line(header))
    print("|" + "|".join("-" * (w + 2) for w in widths) + "|")
    for row in rows:
        print(line(row))


if __name__ == "__main__":
    main(sys.argv[1])
