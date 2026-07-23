# Getting started

## 1. Inspect the environment

```sh
solar --version
solar doctor --all
```

For the default Verilog flow, install Icarus Verilog and Yosys before running a
real simulation or synthesis. Viewers are optional.

## 2. Create a Verilog project

```sh
mkdir counter
cd counter
solar init
```

The template creates `solar.toml`, authored source directories `rtl/` and
`tb/`, public artifact directories `sim/` and `synth/`, and a project-local
`.solar/` workspace when commands need it.

## 3. Discover and validate

```sh
solar scan
solar check
solar build sim --list
```

Project loading always discovers `.v` and `.sv` files below the conventional
directories. `scan` explicitly writes that inventory and inferred tops into
the manifest. It is transactional: an ambiguous or invalid result leaves
`solar.toml` unchanged.

When several tests exist, select a default already listed by Solar:

```sh
solar config set --test basic
```

`config set --test` does not discover a new test. Add or edit the testbench,
run `solar scan`, and then select its discovered name.

## 4. Simulate and synthesize

```sh
solar build sim
solar build synth
solar report
```

The testbench controls waveform creation with a literal call such as:

```verilog
$dumpfile("counter.vcd");
$dumpvars(0, counter_tb);
```

If you later change the dumpfile name or directory, run `solar scan` again so
the test's expected waveform remains synchronized. Solar builds the waveform
inside the isolated test workspace and publishes the validated VCD/FST in
`sim/`.

Synthesis uses authored RTL only and never includes testbench sources. The
netlist and report are published in `synth/`.

## 5. Work with artifacts

```sh
solar view basic
solar view basic --viewer surfer
solar report
solar clean
```

`view` opens an already registered waveform and never starts a build. `report`
reads the last persisted build report. `clean` removes only registered outputs
and selected internal state; user files and directories remain.

## SAPHO/YANC project

```sh
mkdir sapho-demo
cd sapho-demo
solar init --template sapho
solar check
solar build full
```

The SAPHO template creates a standard CMM project. Explicit C++ and Assembly
manifests remain supported; see [YANC/SAPHO projects](yanc.md).
