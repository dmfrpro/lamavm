#!/usr/bin/env python3

import argparse
import os
from pathlib import Path
import shutil
import sys

from vm_report import (
    ROOT,
    diagnostic,
    display_result,
    executable,
    normalize,
    render_report,
    run_measurement,
)

VM_ORDER = ("custom-vm", "lamavm", "ocaml-s")
OUTPUT_NAMES = {"custom-vm": "custom", "lamavm": "lamavm", "ocaml-s": "ocaml"}


class Benchmark:
    def __init__(self, work: Path, timeout: int):
        self.work = work
        self.timeout = timeout
        self.lama_dir = os.environ.get("LAMA_DIR")
        if not self.lama_dir:
            raise SystemExit("LAMA_DIR is not set; run 'nix develop' first")
        self.lama_dir = Path(self.lama_dir)
        self.lamac = executable(os.environ.get("LAMAC") or "lamac", "lamac")
        self.custom = executable(
            os.environ.get("ITER")
            or str(ROOT / "packages/lama-vm/build/lama-custom-vm"),
            "lama-custom-vm (run 'make -C packages/lama-vm' first)",
        )
        self.reference = executable(os.environ.get("REF") or "lamavm", "lamavm")

    def compile(self, source: Path, name: str) -> None:
        shutil.copyfile(source, self.work / f"{name}.lama")
        result = run_measurement(
            [
                self.lamac,
                "-I",
                str(self.lama_dir / "runtime"),
                "-I",
                str(self.lama_dir / "stdlib/x64"),
                "-b",
                f"{name}.lama",
            ],
            self.work / f"{name}.compile.log",
            timeout=self.timeout,
            cwd=self.work,
        )
        if result.returncode:
            message = diagnostic(result.stderr) or display_result(result)
            raise SystemExit(f"cannot compile {name}:\n{message}")

    def commands(self, source: Path, name: str) -> dict[str, list[str]]:
        include = ["-I", str(self.lama_dir / "stdlib/x64")]
        bytecode = str(self.work / f"{name}.bc")
        return {
            "custom-vm": [self.custom, *include, bytecode],
            "lamavm": [self.reference, *include, bytecode],
            "ocaml-s": [
                self.lamac,
                "-I",
                str(self.lama_dir / "runtime"),
                *include,
                "-s",
                str(source),
            ],
        }

    def run(self, source: Path) -> tuple[list[dict], list[dict], str]:
        name = source.stem
        self.compile(source, name)
        results = {}
        for vm, command in self.commands(source, name).items():
            output_name = OUTPUT_NAMES[vm]
            results[vm] = run_measurement(
                command,
                self.work / f"{name}.{output_name}.out",
                timeout=self.timeout,
            )

        expected = normalize(results["custom-vm"].stdout)
        matches = all(normalize(result.stdout) == expected for result in results.values())
        passed = matches and all(result.returncode == 0 for result in results.values())
        status = "PASS" if passed else "FAIL"
        baseline = results["custom-vm"].time_ns
        rows = []
        performance = []
        for vm in VM_ORDER:
            result = results[vm]
            rows.append(
                {
                    "benchmark": name,
                    "vm": vm,
                    "status": status,
                    "time_ns": result.time_ns,
                    "vs_custom": f"{result.time_ns / baseline:.2f}x"
                    if baseline
                    else None,
                    "output": display_result(result),
                }
            )
            performance.append(
                {
                    "benchmark": name,
                    "vm": vm,
                    **result.stats,
                }
            )
        return rows, performance, status


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("benchmark", nargs="?")
    parser.add_argument("--list", "-l", action="store_true")
    args = parser.parse_args()

    sources = sorted((ROOT / "bench").glob("*.lama"))
    names = {source.stem: source for source in sources}
    if args.list:
        print("\n".join(names))
        return 0
    if args.benchmark and args.benchmark not in names:
        parser.error(f"unknown benchmark: {args.benchmark}")

    work = Path(os.environ.get("WORK", ROOT / "build" / "bench"))
    work.mkdir(parents=True, exist_ok=True)
    timeout = int(os.environ.get("BENCH_TIMEOUT", "30"))
    benchmark = Benchmark(work, timeout)
    selected = [names[args.benchmark]] if args.benchmark else sources
    results = []
    performance = []
    statuses = []
    for source in selected:
        rows, counters, status = benchmark.run(source)
        results.extend(rows)
        performance.extend(counters)
        statuses.append(status)

    sections = [
        {
            "title": "Benchmarks",
            "results": results,
            "performance": performance,
        }
    ]
    summary = {
        "passed": statuses.count("PASS"),
        "failed": statuses.count("FAIL"),
    }
    render_report(sections, summary)
    return int("FAIL" in statuses)


if __name__ == "__main__":
    sys.exit(main())
