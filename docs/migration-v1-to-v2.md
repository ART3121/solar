# Migrating project format 1 to format 2

## Compatibility behavior

A manifest without `[solar].format` is treated as format 1. Solar does not edit
it. During loading, Core creates an in-memory test named `default`:

- `[sources].testbench` becomes the test's sources;
- `[simulation].top` becomes the test top;
- `[simulation].waveform` is preserved;
- `[project].top` remains available to the legacy model;
- `[project].default_test` becomes `default` internally.

Commands print a discrete warning:

```text
solar: warning: solar.toml uses project format 1
hint: new projects should use [solar] format = 2
```

Format-1 waveform settings such as `.solar/build/sim/...` remain accepted, but
new simulations publish the validated waveform in `sim/` and keep executables
and logs internal to the current `.solar/` layout.
There is no automatic rewrite and no requirement to migrate immediately.

## Manual migration

Start by adding the format marker:

```toml
[solar]
format = 2
```

Then change the project and source tables. Remove `[project].top`,
`[sources].testbench`, `[simulation].top`, and `[simulation].waveform`; those
legacy test fields are rejected in an explicit format-2 manifest.

Format 1:

```toml
[project]
name = "counter"
top = "counter"
language = "verilog"

[sources]
rtl = ["rtl/counter.v"]
testbench = ["tb/counter_tb.v"]

[simulation]
backend = "iverilog"
top = "counter_tb"
waveform = ".solar/build/sim/counter.vcd"

[synthesis]
backend = "yosys"
top = "counter"
```

Equivalent format 2:

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

The format-2 waveform name is published below `sim/`. Format-1 waveform
settings are still parsed for compatibility, but new operations publish the
validated result in `sim/` and do not rewrite the manifest.

`solar config set` intentionally does not edit format-1 manifests because
their project/simulation top fields do not map unambiguously to named tests.
After the guarded `solar scan` migration succeeds, the format-2 project can use
`--name`, `--top`, and `--test` normally.

## Verification

After editing, run:

```bash
solar check
solar scan
solar build sim --list
solar build sim
solar build synth
```

`check` does not invoke Icarus, Yosys, or YANC. It validates defaults, profiles,
tests, files, include directories, defines, backends, and path safety.

## Non-goals

Format 2 still uses explicit source lists and the Solar TOML subset. Migration
does not add globs, arbitrary flags, package resolution, or full TOML syntax.
