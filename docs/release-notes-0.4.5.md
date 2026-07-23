# Solar 0.4.5

Solar 0.4.5 is the first stable public release of the lightweight open hardware
design platform and EDA workflow orchestrator for Linux.

## Highlights

- Native C17 Core and thin CLI with project validation, deterministic
  discovery, profiles, named tests, safe process execution, artifact ownership,
  and actionable build reports.
- Verilog/SystemVerilog elaboration and simulation through Icarus or Verilator,
  synthesis through Yosys, and optional cocotb 2.x tests.
- Bundled YANC 5.2 compiler flow for CMM, C++, and Assembly SAPHO projects.
- Explicit GTKWave or Surfer waveform viewing; builds never open a GUI.
- Transactional `solar scan` and `solar config set`, safe `solar clean`, and
  visible artifacts in role-specific project directories.

## Install

```sh
curl -fsSL https://github.com/ART3121/solar/releases/latest/download/install.sh | sh
```

The prebuilt asset targets GNU/Linux x86_64 with glibc 2.35 or newer. It
installs below `~/.local` without sudo and includes the private YANC bundle.
Icarus, Yosys, Verilator, cocotb, GTKWave, and Surfer remain user-managed.

Verify downloaded assets with `SHA256SUMS` and inspect optional tools with
`solar doctor --all`.

## Compatibility and limitations

Project manifest format remains 2 and format-1 projects continue to normalize
without automatic rewrites. Solar supports trusted local project inputs; the
bundled legacy YANC compiler is not a sandbox for hostile multi-tenant source.
ARM64, Alpine/musl, macOS, Windows, FPGA programming, and ASIC flows are not
part of this release.

## Resumo em português

O Solar 0.4.5 é o primeiro lançamento público estável para Linux x86_64. O
instalador verifica o checksum, grava em `~/.local` sem `sudo` e não instala
ferramentas EDA do sistema. Consulte os
[primeiros passos em PT-BR](https://art3121.github.io/solar/pt-BR/getting-started.html).
