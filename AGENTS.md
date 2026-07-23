# AGENTS.md — Solar EDA

## 1. Mission

Solar is a lightweight, open, modular hardware-design platform for Linux.

Solar is **not** a new HDL simulator, synthesizer, waveform viewer, or monolithic IDE. Its job is to provide one coherent project model and one small native interface that validates a hardware project, invokes established EDA tools, organizes artifacts, and reports failures clearly.

The initial product is:

- **Solar Core**: reusable orchestration and project-management library written in C.
- **Solar CLI**: terminal interface built on Solar Core.
- **Solar Backends**: isolated adapters for external EDA tools.
- **External tools**: Icarus Verilog and Yosys in the first supported flow.
- **Solar Studio**: a future graphical layer. It must consume the Core API and must not be required by the Core or CLI.

The project name and primary brand are **Solar**. Do not introduce a second umbrella brand such as Orbit.

## 2. Product principles

Every change must preserve these principles:

1. **Small footprint**  
   Prefer a small native binary, modest memory use, fast startup, and few dependencies.

2. **Core first**  
   All reusable behavior belongs in Solar Core. The CLI is a thin adapter. A future GUI must call the same Core API.

3. **Orchestration, not reinvention**  
   Do not implement an HDL compiler, simulator, synthesizer, place-and-route engine, or waveform viewer inside Solar.

4. **Backend isolation**  
   Tool-specific command construction, capability checks, and output interpretation belong in `src/backends/`.

5. **Explicit behavior**  
   Avoid hidden project mutations, implicit downloads, global configuration changes, and surprising tool invocations.

6. **Safe process execution**  
   Never construct untrusted shell command strings. Do not use `system()` for backend execution. Build argument vectors and use a process-running abstraction based on `fork`/`execvp`/`waitpid` or an equivalent safe native mechanism.

7. **Beginner-readable C**  
   The maintainer is learning C. Prefer clear control flow, small functions, explicit ownership, descriptive names, and comments that explain non-obvious decisions rather than narrating syntax.

8. **Incremental delivery**  
   Build one complete vertical slice at a time. A command is not complete until its behavior, errors, tests, and documentation are present.

## 3. Current CLI milestone

Solar remains a CLI-only Linux application. The v0.4.5 public surface is:

- Project: `solar init`, `solar scan`, `solar config`, `solar check`,
  `solar doctor`, and `solar clean`.
- Build: `solar build rtl`, `solar build sim`, `solar build synth`, and
  `solar build full`.
- Artifacts: `solar view` and `solar report`.

`build sim` accepts one name, `--all`, or `--list`. Every executable build
target accepts optional `--profile` and `--dry-run`. The old direct operation
commands (`generate`, `compile`, `test`, `sim`, `simulate`, `synth`,
`synthesize`, and `up`) are not aliases and are not part of the CLI.

The supported backend categories are:

- Icarus Verilog: compile and run Verilog simulations.
- Yosys: synthesize Verilog designs and produce reports/artifacts.
- YANC: compile one CMM, C++, or Assembly source into SAPHO hardware artifacts.

YANC is a compiler toolchain backend, never a simulation backend. Real CMM,
C++, and Assembly flows must be validated separately from fake-executable
tests. A compatibility sidecar may be generated only when its content is
derived from validated upstream output and the limitation is documented.

GTKWave and Surfer may be documented as external viewers, but Solar must not
require either to build or simulate. Automatic GUI launching is outside the
initial milestone; viewers run only through `solar view`.

Commands such as `verify`, FPGA programming, ASIC flows, language servers,
editors, and Solar Studio are later milestones unless a task explicitly
requests them. Parallel tests, timeouts, automatic downloads,
automatic installation, and arbitrary backend flags are outside v0.4.

## 4. Repository layout

Treat the following as the target structure. Extend it only when the new boundary is justified.

```text
solar/
├── AGENTS.md
├── README.md
├── LICENSE
├── CMakeLists.txt
├── cmake/
├── include/
│   └── solar/
│       ├── solar.h
│       ├── result.h
│       ├── project.h
│       ├── config.h
│       ├── config_edit.h
│       ├── backend.h
│       ├── compiler.h
│       ├── build.h
│       ├── rtl.h
│       ├── scan.h
│       ├── test.h
│       ├── synthesis.h
│       ├── yanc.h
│       ├── runner.h
│       ├── diagnostics.h
│       ├── filesystem.h
│       ├── waveform.h
│       └── log.h
├── src/
│   ├── core/
│   │   ├── project.c
│   │   ├── config.c
│   │   ├── config_edit.c
│   │   ├── config_model.c
│   │   ├── compiler.c
│   │   ├── build.c
│   │   ├── rtl.c
│   │   ├── scan.c
│   │   ├── test.c
│   │   ├── synthesis.c
│   │   ├── runner.c
│   │   ├── diagnostics.c
│   │   ├── filesystem.c
│   │   └── log.c
│   ├── backends/
│   │   ├── iverilog.c
│   │   ├── verilator.c
│   │   ├── yanc.c
│   │   ├── yanc_resolver.c
│   │   └── yosys.c
│   └── cli/
│       ├── main.c
│       ├── commands.h
│       ├── command_init.c
│       ├── command_check.c
│       ├── command_config.c
│       ├── command_doctor.c
│       ├── command_scan.c
│       ├── command_build.c
│       ├── command_view.c
│       ├── command_report.c
│       └── command_clean.c
├── tests/
│   ├── test_project.c
│   ├── test_config.c
│   ├── test_config_edit.c
│   ├── test_runner.c
│   ├── test_filesystem.c
│   └── fixtures/
├── examples/
│   └── counter/
│       ├── solar.toml
│       ├── rtl/
│       │   └── counter.v
│       └── tb/
│           └── counter_tb.v
└── docs/
    ├── architecture.md
    ├── project-format.md
    ├── backend-contract.md
    ├── compiler-backends.md
    ├── yanc.md
    ├── profiles.md
    ├── testing.md
    ├── migration-v1-to-v2.md
    ├── implementation-plan.md
    └── roadmap.md
```

The v0.4 boundary includes Build, RTL, Scan, Compiler, Test, and Synthesis
Services; isolated YANC/Icarus/Verilator/Yosys backends; and thin command files
for the current public surface. Keep those responsibilities in their named
modules rather than folding them into CLI files.

Do not create `src/studio/` until the Core and CLI milestone is working and a GUI task explicitly requires it.

## 5. Project manifest

The project manifest is named `solar.toml`.

The parser implements only the documented Solar subset of TOML. Do not claim
full TOML compliance unless a complete compliant parser is used and tested.

New projects use an explicit format marker and named tests:

```toml
[solar]
format = 2

[project]
name = "counter"
language = "verilog"
default_test = "basic"
default_profile = "debug"

[sources]
rtl = ["rtl/counter.v"]
include_dirs = ["rtl/include"]
defines = ["DATA_WIDTH=8"]

[simulation]
backend = "iverilog"

[synthesis]
backend = "yosys"
top = "counter"

[[profile]]
name = "debug"
defines = ["DEBUG"]

[[test]]
name = "basic"
top = "counter_tb"
sources = ["tb/counter_tb.v"]
waveform = "counter.vcd"
```

When `[solar].format` is absent, treat the manifest as format 1, normalize it
in memory to one test named `default`, preserve legacy behavior, print a
discrete warning, and never rewrite the user's file.

Rules:

- Paths are relative to the directory containing `solar.toml`.
- Format-2 Verilog projects discover `.v` and `.sv` files below `rtl/` and `tb/`
  deterministically without shell glob expansion. Explicit paths remain
  supported for unconventional layouts and overrides.
- Unknown required values are errors.
- Unknown optional keys may produce a warning, but must not silently alter behavior.
- Validation errors must include the manifest path, section/key when known, and a concrete correction.
- Parsing and semantic validation are separate operations.
- The parser owns no hidden global state.
- Effective configuration is global, then selected profile, then selected test;
  synthesis omits test-specific configuration.
- YANC projects have one implicit test named `generated` and pass structured
  artifacts to simulation/synthesis consumers.

## 6. Generated project data

Useful build artifacts belong in visible, role-specific project directories.
Logs and disposable workspaces remain under `.solar/`.

Recommended layout:

```text
Verilog: rtl/ tb/ sim/ synth/ .solar/
SAPHO:   software/ hardware/ simulation/ .solar/
```

Rules:

- CMM/C++ generation publishes registered `software/<processor>.asm`; direct
  Assembly input remains user-authored and must never be overwritten or owned
  by cleanup.
- Never scatter generated artifacts through the source tree.
- Never write to system directories during ordinary project commands.
- `solar clean` removes only registered public artifacts and selected
  project-local `.solar/` state.
- Before recursive deletion, verify that each target is exactly one of those
  owned directories inside the active project and is neither `/`, the home
  directory, nor the project root.
- Preserve source files, public directories, unregistered files,
  `solar.toml`, and user-authored reports.
- A pre-existing `solar-build/` is a warned legacy layout. Never create,
  migrate, modify, or delete it automatically.
- Compiler publication must replace the final build only after required
  artifacts are readable and complete; a failed build preserves the previous
  valid publication.

## 7. Architecture boundaries

### Solar Core

Solar Core is responsible for:

- locating the project root and `solar.toml`;
- parsing and validating configuration;
- representing projects and tool settings;
- selecting named tests and profiles and building effective configuration;
- orchestrating Test, Compiler, Simulation, and Synthesis Services;
- returning generated artifacts by role rather than backend directory guesses;
- checking paths and creating project-local output directories;
- running external processes safely;
- collecting exit status, stdout, and stderr;
- dispatching registered backends;
- producing structured diagnostics;
- exposing reusable functions to the CLI and future clients.

Solar Core must not:

- print normal user-interface text directly, except through an injected or explicitly configured logging/diagnostic interface;
- call `exit()`;
- parse CLI arguments;
- contain Icarus-, Yosys-, or YANC-specific command construction outside the
  corresponding backend module;
- depend on a GUI toolkit.

### Solar CLI

The CLI is responsible for:

- parsing command-line arguments;
- converting Core results into concise terminal output;
- selecting exit codes;
- displaying help and version information;
- remaining thin enough that the same operations could be invoked by another frontend.

Do not duplicate project parsing, backend execution, or filesystem policy inside command files.

### Backends

A backend is responsible for:

- declaring its name and capabilities;
- checking whether its external executable is available;
- validating backend-specific prerequisites;
- constructing process argument vectors;
- selecting deterministic artifact paths;
- invoking the shared runner;
- translating tool failures into Solar diagnostics without hiding the original output.

Compiler backends additionally own toolchain layout resolution, exact stage
order, required-output interpretation, and normalization of native outputs.
Icarus/Yosys must consume structured generated artifacts and must not scan a
YANC workspace.

A backend must not parse CLI options, modify global environment configuration, download tools, or write outside the project-local output directory.

## 8. C conventions

Use C17 unless the repository explicitly changes the language standard.

### Naming

- Public functions: `solar_<module>_<operation>()`
- Public types: `SolarProject`, `SolarResult`, `SolarDiagnostic`
- Enum constants and compile-time macros: `SOLAR_UPPER_SNAKE_CASE`
- Internal static functions: descriptive `snake_case`
- Public headers live in `include/solar/`
- Internal declarations remain private to `src/` whenever possible

### API behavior

- Library functions return a `SolarResult` or another explicit status type.
- Do not use `exit()` or `abort()` in library code.
- Distinguish user/configuration errors, missing tools, process failures, I/O failures, invalid arguments, and internal failures.
- Document who owns returned memory.
- Check every allocation and every relevant system-call result.
- Initialize output parameters before operations that may fail.
- Free resources along all error paths.
- Prefer a single cleanup section with `goto cleanup` when it makes ownership easier to audit.
- Avoid mutable global state.
- Avoid fixed-size path buffers when paths can be allocated dynamically.
- Do not expose backend implementation details through the public API.

### Readability

- Keep functions focused and normally below roughly 60 lines; split them when they perform multiple conceptual operations.
- Use early validation to reduce nesting.
- Comments must explain intent, invariants, ownership, or platform constraints.
- Code identifiers and API names are in English.
- User-facing explanations and learning notes may be written in Brazilian Portuguese.
- Do not compress logic into clever macros.

## 9. Dependencies

The default position is no new production dependency.

Before adding one:

1. Confirm the requirement cannot be met cleanly with the standard C library and Linux/POSIX APIs.
2. Explain binary-size, maintenance, licensing, portability, and packaging impact.
3. Prefer a small, well-maintained dependency with a stable C API.
4. Keep the dependency behind one internal module.
5. Update README, build files, and license notices.

Do not add Electron, Node.js, Rust, Tauri, Qt, GTK, Python, or a web stack to Solar Core or Solar CLI.

A future Solar Studio may use a separate technology choice, but it must remain optional and communicate through a stable Core boundary.

## 10. Build and verification

Canonical local workflow:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

When available, also run:

```bash
cmake -S . -B build-asan \
  -DCMAKE_BUILD_TYPE=Debug \
  -DSOLAR_ENABLE_SANITIZERS=ON
cmake --build build-asan
ctest --test-dir build-asan --output-on-failure
```

Build requirements:

- GCC and Clang should both be supported on Linux.
- Enable useful warnings for project targets.
- Do not make platform/library warnings from third-party code fatal.
- Sanitizers are opt-in and must not be enabled in release artifacts.
- Unit tests must not require Icarus Verilog, Yosys, or a real YANC installation.
- Integration tests may detect and skip unavailable external tools, but must state clearly that they were skipped.
- Real YANC integration is opt-in through `SOLAR_TEST_YANC_ROOT`; fake tools do
  not justify a claim of real CMM, C++, or Assembly support.

For any code change, run the smallest relevant tests first and the complete test suite before declaring completion.

## 11. Testing expectations

Every behavior change requires appropriate tests.

At minimum, cover:

- valid and invalid manifest parsing;
- missing sections and keys;
- relative path resolution;
- project-root discovery;
- safe `.solar/` directory creation and cleanup;
- process success, non-zero exit, missing executable, and captured output;
- backend command construction;
- `solar check` diagnostics;
- CLI exit-code mapping;
- format-1 normalization and format-2 repeated tests/profiles;
- effective include/define merge order, duplicate removal, and immutability;
- named/default test and profile selection;
- test artifact isolation and sequential failure summaries;
- compiler backend resolution, stage order/argv, failure stop, required
  artifacts, and atomic publication;
- generated-artifact simulation/synthesis without testbench leakage.

Use fixtures under `tests/fixtures/`. Tests must create temporary directories and must not depend on the user's home directory, network, or existing HDL projects.

A regression fix is incomplete until a test reproduces the previous failure.

## 12. Diagnostics and CLI behavior

Diagnostics should be actionable.

Bad:

```text
Error: project invalid
```

Good:

```text
solar: error: solar.toml: [project].top is required
hint: add top = "counter" under [project]
```

Conventions:

- `stdout`: successful command output intended for users or scripts.
- `stderr`: warnings, errors, and external-tool failure context.
- Exit `0`: success.
- Non-zero: failure. Keep exit-code mapping centralized and documented.
- Preserve the most useful external-tool stderr. Do not replace it with a generic Solar message.
- Avoid color when output is not a terminal or when `NO_COLOR` is set.
- Machine-readable output is a future feature unless explicitly requested.

## 13. Command semantics

### `solar init`

- Refuse to overwrite existing project files unless a future explicit `--force` option is supplied.
- Create a minimal directory layout and documented `solar.toml`.
- Keep the generated example synthesizable and simulatable.
- Support `--template verilog` and `--template sapho`, with Verilog as the
  default. The SAPHO template creates the standard CMM project; C++ and
  Assembly remain supported languages without separate public init templates.
- Format-2 and YANC templates must use only verified language syntax; do not
  invent CMM or Assembly examples.

### `solar check`

- Parse and semantically validate `solar.toml`.
- Confirm required source files exist.
- Confirm selected backends are known.
- Do not execute a simulation or synthesis.
- Missing external tools may be warnings here; `doctor` owns full tool diagnosis.
- Validate tests, profiles, includes, defines, compiler fields, and YANC path
  safety without executing a tool.

### `solar config`

- `solar config set` may update `--name`, `--top`, and `--test` in one
  transaction. `--top` owns `[synthesis].top`; `--test` owns
  `[project].default_test` and must name a selectable test.
- Preserve text outside the managed keys, validate a project-local candidate,
  and atomically replace `solar.toml` only after success.
- Require format 2. A format-1 project must use `solar scan` for its explicit,
  guarded migration before configuration editing.

### `solar doctor`

- Check required and optional external executables.
- Report discovered executable paths and versions when obtainable safely,
  including optional GTKWave and Surfer viewers under `doctor --all`.
- Explain installation responsibility without automatically installing anything.
- A missing tool must not crash Solar.
- Report a resolved YANC root, layout, version, executables, HDL, macros, and
  headers without running setup or installation scripts.

### `solar scan`

- Recursively discover `.v` and `.sv` below conventional `rtl/` and `tb/`
  directories without following symlinks.
- Transactionally synchronize only managed RTL/test fields in `solar.toml`;
  validate a candidate before atomic replacement.
- Preserve explicit unconventional sources, unmanaged test tables, and text
  outside managed blocks. Refuse ambiguous or unsafe rewrites unchanged.
- Project loading still discovers automatically; scan is never a build
  prerequisite.

### `solar build rtl`

- Validate first. For Verilog, elaborate through Icarus compile-only or
  Verilator lint-only. For YANC, run the configured Compiler Service.
- Validate generated artifacts before atomic public publication and retain
  detailed logs below `.solar/`.

### `solar build sim`

- Select an explicit name, then `default_test`, then a sole declared test.
- `--list` executes no backend; `--all` runs sequentially and continues after
  failures before returning an aggregate failure.
- Run RTL first, isolate executables/waveforms/logs by test, and publish only
  VCD/FST output in `sim/` or `simulation/`.
- Never open a waveform viewer. Viewing is a separate `solar view` operation.

### `solar build synth`

- Run RTL first, then generate a deterministic Yosys script below
  `.solar/tmp/` and invoke Yosys through the shared runner.
- Publish Verilog netlist/report in `synth/`, or SAPHO results in `hardware/`.
- Never include testbench files. Apply only global and selected-profile
  includes/defines.

### `solar build full`

- Execute check, RTL, every simulation in manifest order, then synthesis.
- Reuse one YANC compiler result across simulation and synthesis.
- Stop at the first failed simulation and do not synthesize after it.
- `--dry-run` validates and reports planned stages without executing tools.

### `solar view` and `solar report`

- `view` opens an already registered waveform and never builds. GTKWave is the
  compatibility default; `--viewer surfer` selects the optional Surfer client.
- `report` reads `.solar/state/last-report.txt`, never executes a build, and
  succeeds when it can display a recorded failed build.

### `solar clean`

- Remove only registered generated files and selected `.solar/` contents after
  safety validation.
- Succeed cleanly when there is nothing to remove.
- Never follow an unsafe symlink during recursive deletion.

## 14. Documentation requirements

Update documentation with behavior changes.

- `README.md`: purpose, status, prerequisites, build, quick start.
- `docs/architecture.md`: boundaries and data flow.
- `docs/project-format.md`: supported `solar.toml` subset and examples.
- `docs/backend-contract.md`: backend interface and lifecycle.
- `docs/compiler-backends.md`: compiler lifecycle and generated-artifact contract.
- `docs/yanc.md`: external installation, inspected layouts/versions, exact
  verified argv, outputs, divergences, and compatibility constraints.
- `docs/profiles.md`: selection and effective configuration.
- `docs/testing.md`: unit/integration evidence and skip policy.
- `docs/migration-v1-to-v2.md`: non-mutating format-1 compatibility.
- `docs/roadmap.md`: current and future scope without presenting planned features as implemented.
- `docs/implementation-plan.md`: living milestones, progress, discoveries, and decisions for substantial work.

Do not describe Solar as an IDE in primary product language. Preferred description:

> Solar is a lightweight open hardware design platform and EDA workflow orchestrator.

## 15. Work procedure for Codex

Before editing:

1. Read this file completely.
2. Inspect the current tree, CMake configuration, tests, and relevant docs.
3. Determine whether the task is a small patch or a multi-module change.
4. For a multi-module feature or architectural refactor, create or update `docs/implementation-plan.md` before implementation.
5. State assumptions in the plan rather than silently inventing behavior.

During implementation:

1. Preserve existing public behavior unless the task explicitly changes it.
2. Implement from the Core outward: data model → service → backend → CLI → docs.
3. Keep patches focused; do not rewrite unrelated files.
4. Add tests alongside behavior.
5. Run checks as soon as a vertical slice is executable.
6. Fix root causes rather than weakening tests or suppressing errors.

Before finishing:

1. Run build and tests.
2. Run the relevant example flow when its external backend exists.
3. Review the diff for unsafe process calls, path deletion risks, leaks, duplicated logic, and architecture violations.
4. Update docs and the implementation plan.
5. Report:
   - what changed;
   - architectural decisions;
   - commands run;
   - tests passed or skipped;
   - known limitations;
   - the next logical milestone.

Do not claim a command works unless it was built and exercised or a concrete environment limitation prevented execution.

## 16. Prohibited shortcuts

Do not:

- use `system()` or shell concatenation for tool execution;
- mix CLI parsing into Core modules;
- place Icarus or Yosys conditionals throughout generic Core code;
- silently install packages;
- download binaries at runtime;
- add a GUI before the CLI/Core milestone works;
- reimplement EDA engines;
- delete public files that are not registered Solar artifacts;
- hide failed tests by disabling them;
- claim full TOML support from a partial parser;
- make broad refactors unrelated to the current task;
- change the Solar name or introduce Orbit as a parallel product;
- optimize prematurely at the cost of correctness, while still avoiding obviously wasteful designs.

## 17. Definition of done

A change is done only when:

- the requested behavior exists end to end;
- architecture boundaries remain intact;
- errors are actionable;
- memory and resource ownership are clear;
- tests cover normal and failure paths;
- the project builds without new warnings from Solar code;
- relevant tests pass;
- external-tool tests are run or explicitly reported as skipped;
- fake-tool tests and real-tool evidence are reported separately;
- documentation matches the implementation;
- temporary files and logs stay inside `.solar/`, while validated useful
  artifacts stay in the documented public output directories;
- the final report is honest about limitations.
