import json
import os
from pathlib import Path
import resource
import shutil
import subprocess
import time
from dataclasses import dataclass, field
from typing import Sequence

ROOT = Path(__file__).resolve().parent.parent


def empty_usage_stats() -> dict[str, int]:
    return {
        "user_ns": 0,
        "system_ns": 0,
        "page_faults": 0,
        "ctx_switches": 0,
    }


@dataclass
class Result:
    returncode: int
    time_ns: int
    stdout: bytes
    stderr: bytes
    stats: dict[str, int] = field(default_factory=empty_usage_stats)
    timed_out: bool = False


def executable(value: str | None, description: str) -> str:
    if not value:
        raise SystemExit(f"{description} not found; run 'nix develop' first")
    path = shutil.which(value)
    if not path:
        raise SystemExit(f"{description} not found: {value}")
    return path


def normalize(output: bytes) -> str:
    text = output.decode(errors="replace")
    return text.translate(str.maketrans("", "", " >\n\r\t"))


def show_output(output: bytes) -> str:
    return " ".join(output.decode(errors="replace").split())


def display_result(result: Result) -> str:
    output = result.stdout
    if output and result.stderr:
        output += b"\n"
    output += result.stderr
    return show_output(output)


def diagnostic(output: bytes) -> str:
    text = output.decode(errors="replace").strip()
    return "\n".join(text.splitlines()[-3:])


def seconds_to_ns(value: float) -> int:
    return max(0, round(value * 1_000_000_000))


def usage_stats(before: resource.struct_rusage, after: resource.struct_rusage) -> dict[str, int]:
    return {
        "user_ns": seconds_to_ns(after.ru_utime - before.ru_utime),
        "system_ns": seconds_to_ns(after.ru_stime - before.ru_stime),
        "page_faults": max(
            0,
            after.ru_minflt
            - before.ru_minflt
            + after.ru_majflt
            - before.ru_majflt,
        ),
        "ctx_switches": max(
            0,
            after.ru_nvcsw
            - before.ru_nvcsw
            + after.ru_nivcsw
            - before.ru_nivcsw,
        ),
    }


def run_measurement(
    command: Sequence[str],
    output_path: Path,
    *,
    timeout: int,
    input_path: Path | None = None,
    cwd: Path | None = None,
) -> Result:
    stdin = input_path.open("rb") if input_path else subprocess.DEVNULL
    before = resource.getrusage(resource.RUSAGE_CHILDREN)
    start = time.perf_counter_ns()
    result = None
    try:
        completed = subprocess.run(
            command,
            stdin=stdin,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=timeout,
            cwd=cwd,
            check=False,
        )
        result = Result(
            completed.returncode,
            time.perf_counter_ns() - start,
            completed.stdout,
            completed.stderr,
        )
    except subprocess.TimeoutExpired as error:
        result = Result(
            124,
            time.perf_counter_ns() - start,
            error.stdout or b"",
            error.stderr or b"",
            timed_out=True,
        )
    finally:
        after = resource.getrusage(resource.RUSAGE_CHILDREN)
        if result is not None:
            result.stats = usage_stats(before, after)
        if input_path:
            stdin.close()

    output_path.write_bytes(result.stdout)
    output_path.with_suffix(output_path.suffix + ".err").write_bytes(result.stderr)
    return result


def render_report(sections: list[dict], summary: dict | None = None) -> None:
    nu = executable(os.environ.get("NU") or "nu", "nushell")
    renderer = Path(
        os.environ.get("TABLE_RENDERER", ROOT / "scripts" / "render_tables.nu")
    )
    report = {"sections": sections}
    if summary is not None:
        report["summary"] = [summary]
    subprocess.run(
        [nu, "--stdin", str(renderer)],
        input=json.dumps(report),
        text=True,
        check=True,
    )
