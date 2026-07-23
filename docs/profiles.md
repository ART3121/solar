# Profiles and effective configuration

## Purpose

A format-2 profile adds a named set of Verilog include directories and defines
without changing the authored project or test configuration:

```toml
[project]
default_profile = "debug"

[[profile]]
name = "debug"
include_dirs = ["profiles/debug/include"]
defines = ["DEBUG"]

[[profile]]
name = "release"
defines = ["SYNTHESIS"]
```

Profile names use the same safe component rules as test names: letters,
numbers, `_`, `-`, and `.` are accepted; a name cannot be empty, begin with `.`,
contain `/`, or contain `..`. Names must be unique.

## Selection

Solar selects a profile in this order:

```text
--profile <name>
-> [project].default_profile
-> no profile
```

An explicit or default profile must name a declared `[[profile]]`. Commands that
execute work accept profiles as follows:

```bash
solar build rtl --profile debug
solar build sim basic --profile debug
solar build sim --all --profile release
solar build synth --profile release
solar build full --profile release
```

`solar build sim --list` does not accept `--profile` because it executes no work.

## Merge order

For a Verilog test, effective lists are built in this order:

```text
[sources] globals
-> selected [[profile]]
-> selected [[test]]
```

For synthesis, test-specific settings are omitted:

```text
[sources] globals
-> selected [[profile]]
```

Items are compared as exact strings. The first occurrence is retained and later
duplicates are removed without reordering the remaining entries. The original
configuration is never mutated.

Example:

```toml
[sources]
defines = ["DATA_WIDTH=8", "SHARED"]

[[profile]]
name = "debug"
defines = ["DEBUG", "SHARED"]

[[test]]
name = "basic"
top = "counter_tb"
sources = ["tb/counter_tb.v"]
defines = ["TEST_BASIC", "SHARED"]
```

The test receives:

```text
DATA_WIDTH=8
SHARED
DEBUG
TEST_BASIC
```

## Backend mapping

Icarus receives each include as two argv entries (`-I`, absolute directory) and
each define as one `-D<value>` argument. Paths containing spaces remain one
argv item. For Yosys, Solar creates deterministic `includes/N` aliases inside
`.solar/tmp/synth` so `-I` remains one safe token even when the original path
contains spaces; defines are emitted as validated `-D<value>` defaults.

Accepted defines have a Verilog macro name and optional value, for example
`DEBUG`, `DATA_WIDTH=8`, or `FEATURE_NAME=my_feature`. Empty entries, leading
`-`, line breaks, and values containing characters outside ASCII letters,
numbers, and underscores are rejected. A define is data, never a free-form
backend flag.

Include directories are resolved from the manifest directory and must exist as
directories. They may contain spaces but must be safe relative paths.

For YANC projects, profile defines are applied only to the generated Verilog
simulation and synthesis stages. They are not forwarded to `cmmcomp`, `cpppp`,
`cppcomp`, `appcomp`, or `asmcomp` in the current implementation.
