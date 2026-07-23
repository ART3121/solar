# YANC C++ example

This input is adapted from the `CPPComp/Tests/test1` regression fixture in
YANC v5.2 (MIT), with only the configured processor name changed.
The upstream fixture is copyright (c) 2026 NIPSCERN; see the
[YANC license](https://github.com/nipscernlab/yanc/blob/v5.2/LICENSE).
The hardware name `sapho_demo` avoids colliding with YANC's own generic
`processor` HDL module.

Solar uses its bundled YANC by default. The program is compiled by YANC's
`cpppp` and `cppcomp` frontends; Solar does not use the host C++ compiler.
Icarus Verilog and Yosys remain separate external dependencies.
The source includes `software/include/solar_math.hpp`, so this example exercises the
project-local C++ include path through `cpppp`.

```sh
solar check
solar build rtl
solar build sim
solar build synth
solar clean
```

A successful run publishes processor Verilog, MIF, and Yosys outputs in
`hardware/`, generated Assembly as `software/sapho_demo.asm`, plus the
testbench and waveform in `simulation/`. Metadata, logs, and temporary files
remain below `.solar/`.
