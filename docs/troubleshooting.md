# Troubleshooting

## Start with validation and tool discovery

```sh
solar check
solar doctor --all
solar report
```

`check` does not run tools. `doctor` reports executable paths and versions.
`report` remains readable after a failed build and points to detailed logs.

## A simulation passes but no waveform appears

Confirm the selected `[[test]].waveform` matches the literal path passed to
`$dumpfile(...)`. A common sequence is:

1. `solar scan` records `$dumpfile("sim/test")`.
2. The testbench changes to `$dumpfile("sim/test.vcd")`.
3. The manifest is stale, so Solar looks for the old exact name.

Run:

```sh
solar scan
solar check
solar build sim
```

`solar config set --test NAME` only selects a default test and does not rescan
waveform paths. A simulation that intentionally has no `$dumpfile` may pass
without publishing a waveform.

## Multiple tests and no default

List discovered names and select one:

```sh
solar build sim --list
solar config set --test TEST_NAME
```

Alternatively pass a test name to each `solar build sim TEST_NAME` invocation.

## Ambiguous synthesis top

`solar scan` cannot guess among unrelated top-level RTL modules. Correct the
hierarchy or select an intentional synthesis root:

```sh
solar config set --top MODULE_NAME
```

The scan candidate is transactional and does not partially rewrite an
ambiguous manifest.

## Missing tool

Solar never installs EDA packages. Install the executable reported by
`solar doctor`, confirm it is on `PATH`, and retry. A viewer is never required
to build or simulate.

## Verilator rejects a project path

Verilator's generated GNU Make build does not support a workspace path with
whitespace. Move the project to a whitespace-free path or use Icarus. Source
and include paths may still contain spaces.

## Concurrent operation refused

Only one mutating Solar operation may own a project at a time. Wait for the
first build, scan, config edit, or clean to finish. Do not remove lock files or
share one `.solar/` workspace between concurrent commands.

## Reporting a defect

Follow [Solar support](../SUPPORT.md). Preserve the smallest relevant stderr,
the bounded log under `.solar/logs/`, `solar --version`, and `solar doctor`
output. Do not publish proprietary HDL or security-sensitive details.
