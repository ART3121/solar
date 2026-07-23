# Verilator and cocotb example

Requires Verilator 5.036 or newer, Python 3, cocotb 2.x, and optionally
GTKWave. From this directory:

```bash
solar check
solar build sim --list
solar build sim python
```

Solar builds the model below `.solar/tmp/tests/python/`, runs the Python test,
and publishes `sim/adder.fst`. Open it later with `solar view python`.
