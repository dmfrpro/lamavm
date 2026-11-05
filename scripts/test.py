#!/usr/bin/env python3

import argparse
import os
from pathlib import Path
import runpy
import shutil
import sys
import tempfile
from dataclasses import dataclass, field

from vm_report import (
    Result,
    diagnostic,
    display_result,
    executable,
    normalize,
    render_report,
    run_measurement,
    show_output,
)

ROOT = Path(__file__).resolve().parent.parent
NEGATIVE_GENERATOR = Path(
    os.environ.get("NEGATIVE_GENERATOR", ROOT / "scripts" / "make_bad_bc.py")
)


@dataclass
class Outcome:
    status: str
    message: str
    results: dict[str, Result] = field(default_factory=dict)


class Runner:
    def __init__(self, work: Path):
        self.work = work
        self.lama_dir = os.environ.get("LAMA_DIR")
        self.lamac = executable(os.environ.get("LAMAC") or "lamac", "lamac")
        self.iter = executable(
            os.environ.get("ITER")
            or str(ROOT / "packages/lama-vm/build/lama-custom-vm"),
            "lama-custom-vm (run 'make -C packages/lama-vm' first)",
        )
        self.reference_vm = executable(os.environ.get("REF") or "lamavm", "lamavm")
        self.results: dict[str, Result] = {}

    def outcome(self, status: str, message: str) -> Outcome:
        return Outcome(status, message, dict(self.results))

    @property
    def regression_dir(self) -> Path:
        if not self.lama_dir:
            raise SystemExit("LAMA_DIR is not set; run 'nix develop' first")
        return Path(self.lama_dir) / "regression"

    def compile(self, source: Path, name: str) -> Result:
        if not self.lama_dir:
            raise SystemExit("LAMA_DIR is not set; run 'nix develop' first")
        shutil.copyfile(source, self.work / f"{name}.lama")
        return run_measurement(
            [
                self.lamac,
                "-I",
                str(Path(self.lama_dir) / "runtime"),
                "-I",
                str(Path(self.lama_dir) / "stdlib/x64"),
                "-b",
                f"{name}.lama",
            ],
            self.work / f"{name}.compile.log",
            timeout=120,
            cwd=self.work,
        )

    def reference(self, source: Path, name: str, input_path: Path) -> Result:
        if not self.lama_dir:
            raise SystemExit("LAMA_DIR is not set; run 'nix develop' first")
        return run_measurement(
            [
                self.lamac,
                "-I",
                str(Path(self.lama_dir) / "runtime"),
                "-I",
                str(Path(self.lama_dir) / "stdlib/x64"),
                "-s",
                str(source),
            ],
            self.work / f"{name}.reference.out",
            timeout=120,
            input_path=input_path,
        )

    def bytecode(
        self, interpreter: str, name: str, output_name: str, input_path: Path
    ) -> Result:
        if not self.lama_dir:
            raise SystemExit("LAMA_DIR is not set; run 'nix develop' first")
        command = [
            interpreter,
            "-I",
            str(self.work),
            "-I",
            str(Path(self.lama_dir) / "stdlib/x64"),
            str(self.work / f"{name}.bc"),
        ]
        return run_measurement(
            command,
            self.work / f"{name}.{output_name}.out",
            timeout=60,
            input_path=input_path,
        )

    def custom(self, name: str, input_path: Path) -> Result:
        return self.bytecode(self.iter, name, "custom", input_path)

    def upstream(self, name: str, input_path: Path) -> Result:
        return self.bytecode(self.reference_vm, name, "lamavm", input_path)

    def regression(self, name: str, expect_skip: str | None = None) -> Outcome:
        self.results = {}
        source = self.regression_dir / f"{name}.lama"
        input_path = self.regression_dir / f"{name}.input"
        if not input_path.exists():
            input_path = Path("/dev/null")

        compiled = self.compile(source, name)
        if compiled.returncode:
            message = diagnostic(compiled.stderr) or show_output(compiled.stdout)
            if expect_skip == "compile":
                if "Indirect assignment is not supported yet" not in message:
                    return self.outcome(
                        "fail", f"{name}: unexpected compiler failure: {message}"
                    )
                return self.outcome(
                    "skip", f"{name}: upstream compiler limitation: {message}"
                )
            if expect_skip:
                return self.outcome("fail", f"{name}: bytecode compilation failed: {message}")
            return self.outcome("skip", f"{name}: bytecode compilation failed: {message}")
        if expect_skip == "compile":
            return self.outcome("fail", f"{name}: expected upstream compilation to fail")

        reference = self.reference(source, name, input_path)
        self.results["ocaml-s"] = reference
        if reference.returncode:
            message = diagnostic(reference.stderr)
            if expect_skip == "reference":
                return self.outcome(
                    "skip",
                    f"{name}: reference recursive interpreter exited "
                    f"{reference.returncode}: {message}",
                )
            if expect_skip:
                return self.outcome("fail", f"{name}: unexpected reference failure")
            return self.outcome(
                "skip",
                f"{name}: reference recursive interpreter exited {reference.returncode}",
            )
        if expect_skip == "reference":
            return self.outcome("fail", f"{name}: expected reference interpreter to fail")

        upstream = self.upstream(name, input_path)
        custom = self.custom(name, input_path)
        self.results["lamavm"] = upstream
        self.results["custom-vm"] = custom
        if upstream.returncode:
            reason = "timed out" if upstream.timed_out else f"exited {upstream.returncode}"
            return self.outcome(
                "fail", f"{name}: lamavm {reason}: {diagnostic(upstream.stderr)}"
            )
        if custom.returncode:
            reason = "timed out" if custom.timed_out else f"exited {custom.returncode}"
            return self.outcome(
                "fail", f"{name}: lama-custom-vm {reason}: {diagnostic(custom.stderr)}"
            )

        expected = normalize(reference.stdout)
        if expected != normalize(upstream.stdout) or expected != normalize(custom.stdout):
            (self.work / f"{name}.expected").write_text(expected)
            (self.work / f"{name}.actual").write_text(normalize(custom.stdout))
            return self.outcome(
                "fail",
                f"{name}: output mismatch\nocaml-s: {show_output(reference.stdout)}\n"
                f"lamavm: {show_output(upstream.stdout)}\n"
                f"custom-vm: {show_output(custom.stdout)}",
            )
        return self.outcome(
            "pass",
            f"{name}: {show_output(custom.stdout)}",
        )

    def negative(self, name: str, case: object) -> Outcome:
        self.results = {}
        bytecode = self.work / f"{name}.bc"
        bytecode.write_bytes(case())
        result = run_measurement(
            [self.iter, str(bytecode)],
            self.work / f"{name}.out",
            timeout=60,
        )
        self.results["custom-vm"] = result
        error = result.stderr.decode(errors="replace").strip()
        expected = None
        if name.endswith("_stack_underflow"):
            expected = "operand stack underflow"
        elif name.endswith("by_zero"):
            expected = "by zero"
        elif name == "end_extra_operands":
            expected = "END requires exactly one operand"
        elif name == "closure_capture_count_mismatch":
            expected = "does not match target capture count"

        if result.returncode == 0:
            return self.outcome("fail", f"{name}: accepted malformed input")
        if result.returncode < 0 or result.returncode >= 128 or result.timed_out:
            return self.outcome(
                "fail", f"{name}: crashed or timed out (exit {result.returncode})"
            )
        if not error:
            return self.outcome("fail", f"{name}: produced no diagnostic")
        if expected and (expected not in error or "bytecode offset" not in error):
            return self.outcome("fail", f"{name}: missing instruction diagnostic: {error}")
        return self.outcome(
            "pass",
            f"{name}: custom={result.time_ns}ns diagnostic: {show_output(result.stderr)}",
        )


def regression_names(runner: Runner) -> list[str]:
    return sorted(path.stem for path in runner.regression_dir.glob("test*.lama"))


def negative_cases() -> dict[str, object]:
    return runpy.run_path(str(NEGATIVE_GENERATOR))["CASES"]


VM_ORDER = ("custom-vm", "lamavm", "ocaml-s")


def suite_section(title: str, outcomes: list[Outcome]) -> dict:
    rows = []
    performance = []
    for outcome in outcomes:
        name, _, details = outcome.message.partition(":")
        if not outcome.results:
            rows.append(
                {
                    "test": name,
                    "vm": "-",
                    "status": outcome.status.upper(),
                    "time_ns": None,
                    "vs_custom": None,
                    "output": details.strip(),
                }
            )
            continue

        custom = outcome.results.get("custom-vm")
        baseline = custom.time_ns if custom else None
        for vm in VM_ORDER:
            if vm not in outcome.results:
                continue
            result = outcome.results[vm]
            rows.append(
                {
                    "test": name,
                    "vm": vm,
                    "status": outcome.status.upper(),
                    "time_ns": result.time_ns,
                    "vs_custom": f"{result.time_ns / baseline:.2f}x"
                    if baseline
                    else None,
                    "output": display_result(result),
                }
            )
            performance.append({"test": name, "vm": vm, **result.stats})
    return {"title": title, "results": rows, "performance": performance}


def summary(outcomes: list[Outcome]) -> dict:
    return {
        "passed": sum(outcome.status == "pass" for outcome in outcomes),
        "failed": sum(outcome.status == "fail" for outcome in outcomes),
        "skipped": sum(outcome.status == "skip" for outcome in outcomes),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("suite", nargs="?", default="all")
    parser.add_argument("names", nargs="*")
    parser.add_argument("--expect-skip", choices=("compile", "reference"))
    parser.add_argument("--status-file", type=Path)
    parser.add_argument("--work", type=Path)
    args = parser.parse_args()

    suites = {"all", "regression", "negative"}
    if args.suite not in suites:
        args.names.insert(0, args.suite)
        args.suite = "regression"
    if args.names and args.suite == "all":
        parser.error("test names require the regression or negative suite")
    if args.expect_skip and (args.suite != "regression" or len(args.names) != 1):
        parser.error("--expect-skip requires exactly one regression test")
    if args.status_file and (args.suite == "all" or len(args.names) != 1):
        parser.error("--status-file requires exactly one test")

    if args.work:
        work = args.work.resolve()
        work.mkdir(parents=True, exist_ok=True)
        temporary = None
    else:
        temporary = tempfile.TemporaryDirectory(prefix="lama-vm-tests-")
        work = Path(temporary.name)

    try:
        runner = Runner(work)
        outcomes: list[Outcome] = []
        sections = []
        if args.suite in ("all", "regression"):
            available = regression_names(runner)
            names = args.names or available
            unknown = sorted(set(names) - set(available))
            if unknown:
                parser.error(f"unknown regression tests: {', '.join(unknown)}")
            regression_work = work / "regression" if args.suite == "all" else work
            regression_work.mkdir(parents=True, exist_ok=True)
            runner.work = regression_work
            suite_outcomes = []
            for name in names:
                outcome = runner.regression(name, args.expect_skip)
                suite_outcomes.append(outcome)
                if args.status_file:
                    args.status_file.parent.mkdir(parents=True, exist_ok=True)
                    args.status_file.write_text(f"{outcome.status.upper()} {outcome.message}\n")
                if args.expect_skip and outcome.status == "fail":
                    break
            outcomes.extend(suite_outcomes)
            sections.append(suite_section("Regression tests", suite_outcomes))

        if args.suite in ("all", "negative"):
            cases = negative_cases()
            names = args.names or list(cases)
            unknown = sorted(set(names) - set(cases))
            if unknown:
                parser.error(f"unknown malformed-input tests: {', '.join(unknown)}")
            negative_work = work / "negative" if args.suite == "all" else work
            negative_work.mkdir(parents=True, exist_ok=True)
            runner.work = negative_work
            suite_outcomes = []
            for name in names:
                outcome = runner.negative(name, cases[name])
                suite_outcomes.append(outcome)
                if args.status_file:
                    args.status_file.parent.mkdir(parents=True, exist_ok=True)
                    args.status_file.write_text(f"{outcome.status.upper()} {outcome.message}\n")
            outcomes.extend(suite_outcomes)
            sections.append(suite_section("Malformed-input tests", suite_outcomes))

        render_report(sections, summary(outcomes))
        return int(any(outcome.status == "fail" for outcome in outcomes))
    finally:
        if temporary:
            temporary.cleanup()


if __name__ == "__main__":
    sys.exit(main())
