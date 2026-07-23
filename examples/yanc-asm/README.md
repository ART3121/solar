# YANC Assembly example

The source is the Assembly produced by YANC v5.2 from its official C++
`CPPComp/Tests/test1` regression fixture (MIT), with the processor name changed.
The upstream fixture is copyright (c) 2026 NIPSCERN; see the
[YANC license](https://github.com/nipscernlab/yanc/blob/v5.2/LICENSE).
The hardware name `sapho_demo` avoids colliding with YANC's own generic
`processor` HDL module.

The inspected YANC v5.2 `asmcomp` expects frontend sidecar state even for a
direct Assembly input and does not document that standalone contract. Solar's
YANC adapter derives the minimum compatibility metadata from `appcomp` inside
its temporary workspace. This example was compiled, simulated, and synthesized
with the bundled YANC v5.2 toolchain; the workaround remains version-sensitive.

Use the bundled toolchain directly:

```sh
solar check
solar build rtl
solar build sim
solar build synth
solar clean
```

Solar never modifies this source or writes into the YANC installation.
A successful run publishes processor Verilog, MIF, and Yosys outputs in
`hardware/`, plus the generated testbench and waveform in `simulation/`.
The staged Assembly, metadata, and logs remain below `.solar/`.
