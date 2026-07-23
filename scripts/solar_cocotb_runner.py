#!/usr/bin/env python3
"""Small, private bridge from Solar argv to cocotb's Python runner API."""

import argparse
import os
from pathlib import Path
import shutil
import sys
import time


def parse_arguments():
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--top", required=True)
    parser.add_argument("--build-dir", required=True)
    parser.add_argument("--test-file", required=True)
    parser.add_argument("--waveform", required=True)
    parser.add_argument("--timing-file", required=True)
    parser.add_argument("--source", action="append", default=[])
    parser.add_argument("--include", action="append", default=[])
    parser.add_argument("--define", action="append", default=[])
    return parser.parse_args()


def write_timings(path, compile_ns, simulation_ns):
    target = Path(path)
    temporary = target.with_suffix(target.suffix + ".tmp")
    target.parent.mkdir(parents=True, exist_ok=True)
    with temporary.open("w", encoding="ascii") as output:
        output.write(f"compile_ns={compile_ns}\n")
        output.write(f"simulation_ns={simulation_ns}\n")
        output.flush()
        os.fsync(output.fileno())
    temporary.replace(target)


def define_mapping(values):
    result = {}
    for value in values:
        name, separator, definition = value.partition("=")
        result[name] = definition if separator else True
    return result


def main():
    arguments = parse_arguments()
    compile_ns = 0
    simulation_ns = 0
    write_timings(arguments.timing_file, compile_ns, simulation_ns)
    try:
        from cocotb_tools.runner import get_runner
    except ImportError as error:
        print(f"cocotb 2.x is unavailable: {error}", file=sys.stderr)
        return 4

    work = Path(arguments.build_dir).resolve()
    test_file = Path(arguments.test_file).resolve()
    waveform = Path(arguments.waveform).resolve()
    sys.path.insert(0, str(test_file.parent))
    runner = get_runner("verilator")
    build_directory = work / "cocotb-build"
    build_arguments = ["--trace-fst"] if waveform.suffix == ".fst" else []
    compile_started = time.perf_counter_ns()
    try:
        runner.build(
            sources=[Path(source) for source in arguments.source],
            includes=[Path(include) for include in arguments.include],
            defines=define_mapping(arguments.define),
            build_args=build_arguments,
            hdl_toplevel=arguments.top,
            build_dir=build_directory,
            clean=True,
            waves=True,
        )
    except BaseException as error:
        compile_ns = time.perf_counter_ns() - compile_started
        write_timings(arguments.timing_file, compile_ns, simulation_ns)
        print(f"cocotb build failed: {error}", file=sys.stderr)
        return 5
    compile_ns = time.perf_counter_ns() - compile_started
    write_timings(arguments.timing_file, compile_ns, simulation_ns)

    simulation_started = time.perf_counter_ns()
    try:
        runner.test(
            test_module=test_file.stem,
            hdl_toplevel=arguments.top,
            hdl_toplevel_lang="verilog",
            build_dir=build_directory,
            test_dir=work,
            results_xml=work / "cocotb-results.xml",
            waves=True,
        )
    except BaseException as error:
        simulation_ns = time.perf_counter_ns() - simulation_started
        write_timings(arguments.timing_file, compile_ns, simulation_ns)
        print(f"cocotb test failed: {error}", file=sys.stderr)
        return 6
    simulation_ns = time.perf_counter_ns() - simulation_started
    write_timings(arguments.timing_file, compile_ns, simulation_ns)

    generated = work / ("dump.fst" if waveform.suffix == ".fst" else "dump.vcd")
    if generated.is_file():
        waveform.parent.mkdir(parents=True, exist_ok=True)
        if generated != waveform:
            shutil.move(generated, waveform)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
