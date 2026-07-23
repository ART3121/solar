# Solar 0.4.0 to 0.4.5

Solar 0.4.5 is a compatible update to the 0.4 line. It does not change the
Solar TOML subset or its `[solar] format = 2` marker. Product version and
manifest format are independent: existing projects require no rewrite.

## Changes

| Area | Solar 0.4.0 | Solar 0.4.5 |
| --- | --- | --- |
| Waveform viewer | Explicit GTKWave | GTKWave remains default; `solar view ... --viewer surfer` selects the external Surfer client |
| Common manifest edits | Manual editing | `solar config set --name`, `--top`, and `--test` validate and atomically update format-2 manifests |
| YANC CMM/C++ Assembly | Validated but retained below `.solar/tmp/` | Published as registered `software/<processor>.asm` and shown by build/report |
| Direct Assembly input | User-authored source | Unchanged; Solar does not overwrite or register the source |

## Compatibility

- CLI build targets, simulator/synthesizer backends, profiles, and named tests
  keep their 0.4.0 behavior.
- Verilog artifacts remain in `sim/` and `synth/`; SAPHO RTL, MIF, and
  simulation artifacts remain in `hardware/` and `simulation/`.
- The CMM/C++ Assembly file is published transactionally with the other YANC
  outputs. A manual unregistered file at the destination blocks publication.
- `solar clean` removes the registered generated Assembly and preserves every
  unregistered file and the `software/` directory.
- Surfer remains optional and external. Simulation never opens a viewer.

## Command examples

```bash
solar config set --name benchmark-core --top processor --test generated
solar build rtl
solar report
solar view --viewer surfer
```

For CMM and C++ projects, a successful `solar build rtl`, `sim`, `synth`, or
`full` generation makes the Assembly visible as:

```text
software/<processor>.asm
```

No release tag is created by the version metadata update itself.
