# Primeiros passos com o Solar

Solar é uma plataforma leve de hardware aberto e um orquestrador de fluxos EDA
para Linux. Ele usa ferramentas consolidadas, como Icarus Verilog e Yosys, sem
reimplementar simuladores ou sintetizadores.

O manual técnico canônico é mantido em inglês. Esta página cobre instalação e
o primeiro fluxo Verilog.

## Instalação

O release Linux x86_64 é instalado em `~/.local` sem `sudo`:

```sh
curl -fsSL https://github.com/ART3121/solar/releases/latest/download/install.sh | sh
```

Se necessário, adicione o executável ao ambiente:

```sh
export PATH="$HOME/.local/bin:$PATH"
```

O instalador verifica o checksum e instala apenas o Solar e o bundle privado do
YANC. Icarus, Yosys, Verilator, cocotb e visualizadores continuam sendo
instalados pelo usuário por meio da sua distribuição.

```sh
solar --version
solar doctor --all
```

Consulte o [guia completo de instalação](../installation.md) para versões
fixadas, verificação manual, compilação e desinstalação.

## Primeiro projeto

```sh
mkdir counter
cd counter
solar init
solar scan
solar check
solar build sim --list
solar build sim
solar build synth
solar report
```

Arquivos `.v` e `.sv` abaixo de `rtl/` e `tb/` são descobertos
automaticamente. `solar scan` grava no `solar.toml` o inventário, os tops, os
testes e o caminho literal usado em `$dumpfile(...)`.

Se você mudar o nome ou caminho do `$dumpfile`, execute `solar scan` novamente.
`solar config set --test NOME` apenas seleciona um teste já descoberto; ele não
reanalisa o testbench.

Waveforms ficam em `sim/`, netlists e relatórios de síntese em `synth/`, e logs
e temporários em `.solar/`. `solar clean` remove somente artefatos registrados.

Próximas leituras:

- [Manual do usuário](../index.md)
- [Primeiro projeto detalhado](../getting-started.md)
- [Referência de comandos](../command-reference.md)
- [Solução de problemas](../troubleshooting.md)
