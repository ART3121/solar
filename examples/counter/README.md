# Counter example

This format-2 Verilog project demonstrates named tests, a default profile,
include directories, and compile-time defines.

```sh
solar check
solar build sim --list
solar build sim
solar build sim overflow
solar build sim --all --profile debug
solar build synth --profile release
solar report
solar clean
```

Waveforms are published in `sim/`, while Yosys netlists and reports are
published in `synth/`. Executables, scripts, and logs remain internal to
`.solar/`. Icarus Verilog is required for tests, and Yosys for synthesis.
