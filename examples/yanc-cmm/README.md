# YANC CMM example

This input is adapted from the `sw_test` CMM regression fixture in YANC v5.2
(MIT), with only the configured processor name changed to `sapho_demo`.
The upstream fixture is copyright (c) 2026 NIPSCERN; see the
[YANC license](https://github.com/nipscernlab/yanc/blob/v5.2/LICENSE).
The hardware name `sapho_demo` avoids colliding with YANC's own generic
`processor` HDL module.

Solar builds and installs its private YANC bundle. `SOLAR_YANC_ROOT` is needed
only to test a different toolchain checkout. Icarus Verilog and Yosys are
separate external dependencies.

```sh
solar check
solar build rtl
solar build sim generated
solar build synth
solar clean
```

A successful generation publishes processor Verilog and instruction/data MIF
images in `hardware/`, generated Assembly as `software/sapho_demo.asm`, and the
generated testbench in `simulation/`. Build metadata remains internal to
`.solar/`; stage logs use `.solar/logs/`.
