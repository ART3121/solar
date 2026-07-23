# Solar 0.3 to 0.4

Solar 0.4 keeps the v0.3 project model, command surface, supported backend set,
and artifact layouts. The release groups four operational improvements:

1. benchmark-oriented build reports;
2. a hardened and independently tested Verilator backend;
3. RTL-only projects and optional simulation waveforms;
4. observable `solar build sim` progress and immediate stage timings.

This is a behavior and observability release, not a new manifest generation.
`[solar] format = 2` remains current and no manifest rewrite is required.

## What changed

| Area | Solar 0.3 | Solar 0.4 |
| --- | --- | --- |
| Build report | Basic last-build snapshot | Host CPU/memory/OS, compiler and tool versions, configuration, artifacts, logs, readable durations, raw monotonic nanoseconds, and measurement guidance |
| Tool timing | High-level pipeline durations | Separate RTL, YANC-stage, simulator compilation/runtime, cocotb build/test, and Yosys process timings |
| Verilator evidence | Backend and cocotb-oriented coverage | Real standalone Verilator VCD/FST integration, visible `$display`, preserved non-fatal warnings, and an early diagnostic for unsupported workspace whitespace |
| RTL-only projects | Validation expected at least one test | `scan`, `check`, `build rtl`, and `build synth` support projects with no testbench; simulation still requires a selectable test |
| Missing waveform | A successful process could still fail publication | Exit-success remains `PASS`; Solar warns and registers no waveform when no VCD/FST is produced |
| Interactive simulation | Output appeared after synchronous stages | One TTY line is redrawn with percent, stage, backend, spinner, stage time, and total time |
| Verilog schedule | No progress contract | `5/10/20/40/85/95/100` for load, validate, resolve, compile, run, publish, and finish |
| SAPHO simulation | YANC ran without progress events | YANC generation is an explicit earlier range before Icarus compilation |
| Redirected/CI output | Final command output only | Stable `[sim] ...` lines, without ANSI or carriage returns |
| Tool output | Read after execution from logs | `--verbose` tees live stdout/stderr while preserving the logs |
| Progress control | Not available | `--no-progress` suppresses progress; final results and timings remain |
| Long processes | No continuous CLI activity | Percentage stays fixed while spinner and clocks advance |
| Interrupted process | Runner forwarded signals | The same forwarding/reaping behavior is retained with progress active and tested with `SIGINT` |

## Compatibility

- The CLI remains `init`, `scan`, `check`, `doctor`, `clean`, `build
  rtl|sim|synth|full`, `view`, and `report`.
- Format-1 manifests retain their non-mutating compatibility path.
- Existing format-2 manifests need no changes. A project may keep explicit
  tests and waveform names exactly as before.
- Verilog artifacts remain in `sim/` and `synth/`; SAPHO artifacts remain in
  `hardware/` and `simulation/`; internal state remains below `.solar/`.
- Icarus, Verilator, cocotb, Yosys, and YANC keep the same roles.
- Simulation never opens a waveform viewer automatically.

The optional-waveform rule does not convert a failing test into success.
Compilation errors, nonzero simulator exits, logical test failures, unreadable
outputs, and publication errors still fail the operation. It only separates
test success from the optional act of dumping waves.

## Command use

Normal interactive use needs no change:

```bash
solar build sim
solar report
```

For an RTL-only project, discovery and synthesis do not require a dummy
testbench:

```bash
solar scan
solar check
solar build rtl
solar build synth
```

For CI or intentionally quiet simulation:

```bash
solar build sim --no-progress
```

For live tool diagnostics as well as preserved logs:

```bash
solar build sim --verbose
```

Named tests, `--all`, `--profile`, and `--dry-run` remain compatible with the
rules documented by `solar build --help`.

## Core API compatibility

Existing Core operation entry points remain available. Public request/result
structures gained timing fields, and progress-aware variants accept an
optional borrowed observer. External C clients should be recompiled against
the 0.4 headers even when they do not consume timings or progress events.

## Measurement interpretation

Report and simulation-summary durations use monotonic wall-clock time, not CPU
time. They separate Solar-controlled stages and external processes but do not
infer progress inside a simulator. For paper-quality benchmarks, record the
persisted host/tool/profile data, control system load and warm-up policy, and
run multiple repetitions with a documented aggregation method.
