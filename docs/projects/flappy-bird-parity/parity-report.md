---
title: "Flappy Bird Parity Report"
category: project
status: 现行
owner: docs
created: 2026-05-06
last_updated: 2026-05-06
---

# Flappy Bird Parity Report

Status: **PASS**

## Inputs

- C++ trace: `out\flappy_cpp_trace.json`
- JS trace: `out\flappy_js_trace.json`
- C++ implementation: `FlappyCpp`
- JS implementation: `FlappyJs`

## Summary

- fixedDeltaSeconds: `0.01666666753590107`
- rngSeed: `12648430`
- deathFrame: C++ `126`, JS `126`
- frame count: C++ `720`, JS `720`
- exact mismatches: `0`
- max birdY delta: `0.0` at frame `None`
- max velocity delta: `0.0` at frame `None`

## Reproduce

```powershell
.\out\build\full-windows\bin\FlappyCpp.exe --flappy-replay
.\out\build\full-windows\bin\FlappyJs.exe --flappy-replay
python tools\flappy\diff_traces.py --report docs\projects\flappy-bird-parity\parity-report.md
```

## Visual Test Note

`gkNextVisualTest` currently runs scene files through `RequestLoadScene`; it does not launch separate application targets. Flappy parity is therefore validated with executable-level deterministic replay traces for now.

