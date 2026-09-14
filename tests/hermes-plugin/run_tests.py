#!/usr/bin/env python3
"""Repo-contained verification runner for the Hermes plugin.

Run from the project root:

    python3 tests/hermes-plugin/run_tests.py

Stdlib only: no pytest, no third-party packages. Each module runs in its **own process**, because
the suites patch live Hermes module attributes and share process-global registries; running them
in one interpreter makes them contaminate each other.

Hermes-dependent modules are skipped, with an exact skipped count, when no Hermes install can be
imported. To run them:

    HERMES_AGENT_PATH=/path/to/hermes-agent python3 tests/hermes-plugin/run_tests.py

If the current interpreter is too old for Hermes but a Hermes virtualenv exists, each module
re-executes itself once with that interpreter's Python.

Every module uses local fake sockets and playback stubs: nothing contacts the device, nothing is
audible, no external TTS service is called, and no credentials are required.
"""
from __future__ import annotations

import json
import os
import re
import subprocess
import sys
from pathlib import Path

# The plugin under test lives inside this repository, so importing it would otherwise write a
# __pycache__ next to it. Never write bytecode: the suite must not create files in the repo.
sys.dont_write_bytecode = True

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
MODULES = ["test_file_cli_path.py", "test_desktop_stream.py", "test_mdns_resilience.py"]
RESULT_PREFIX = "__AVATAR_RESULT__ "
_REEXEC_FLAG = "_AVATAR_TESTS_REEXEC"

GREEN, RED, YELLOW, DIM, RESET = "\033[32m", "\033[31m", "\033[33m", "\033[2m", "\033[0m"
if not sys.stdout.isatty():
    GREEN = RED = YELLOW = DIM = RESET = ""


class SkipSuite(Exception):
    """Raised before any check runs: the module cannot execute in this environment."""


def find_hermes() -> Path | None:
    env = os.environ.get("HERMES_AGENT_PATH")
    if env and (Path(env) / "tools" / "voice_mode.py").is_file():
        return Path(env)
    conventional = Path.home() / ".hermes" / "hermes-agent"   # Hermes' default install location
    if (conventional / "tools" / "voice_mode.py").is_file():
        return conventional
    return None


def importable(hermes: Path) -> bool:
    if str(hermes) not in sys.path:
        sys.path.insert(0, str(hermes))
    try:
        # Probe the complete Hermes surface used by the suites. Importing only
        # voice_mode can succeed under a system Python that is still missing
        # dependencies needed by hermes_cli.voice or the TTS modules.
        import tools.voice_mode          # noqa: PLC0415,F401
        import hermes_cli.voice           # noqa: PLC0415,F401
        import tools.tts_tool             # noqa: PLC0415,F401
        import tools.tts_streaming        # noqa: PLC0415,F401
        return True
    except Exception:
        return False


def maybe_reexec(hermes: Path) -> None:
    """Too-old interpreter + Hermes venv present -> run this module under that Python, once."""
    if os.environ.get(_REEXEC_FLAG) == "1":
        return
    for candidate in (hermes / "venv" / "bin" / "python", hermes / ".venv" / "bin" / "python"):
        if candidate.is_file():
            print(f"{DIM}re-executing under {candidate}{RESET}")
            env = dict(os.environ, **{_REEXEC_FLAG: "1", "HERMES_AGENT_PATH": str(hermes)})
            os.execve(str(candidate), [str(candidate), str(Path(__file__).resolve()), *sys.argv[1:]], env)


class Harness:
    def __init__(self) -> None:
        self.results: list[tuple[str, bool, str]] = []

    def check(self, name: str, ok, detail: str = "") -> None:
        ok = bool(ok)
        self.results.append((name, ok, "" if ok else str(detail)))
        mark = f"{GREEN}PASS{RESET}" if ok else f"{RED}FAIL{RESET}"
        print(f"  {mark}  {name}" + ("" if ok else f"   {detail}"), flush=True)


def declared_count(path: Path) -> int:
    match = re.search(r"^CHECKS = (\d+)$", path.read_text(), re.M)
    if not match:
        raise SystemExit(f"{path.name} does not declare CHECKS = <n>")
    return int(match.group(1))


def run_one(path: Path) -> dict:
    """Run a single module in THIS process and emit a machine-readable result line."""
    declared = declared_count(path)
    print(f"{DIM}{path.name} ({declared} checks){RESET}", flush=True)

    hermes = find_hermes()
    if hermes is None or not importable(hermes):
        if hermes is not None:
            maybe_reexec(hermes)
        print(f"  {YELLOW}SKIP{RESET}  Hermes not importable; set HERMES_AGENT_PATH to run these",
              flush=True)
        return {"passed": 0, "skipped": declared, "failed": 0, "failures": [], "declared": declared}

    harness = Harness()
    namespace = {
        "__name__": f"avatar_tests_{path.stem}",
        "__file__": str(path),
        "check": harness.check,
        "REPO": REPO,
        "HERMES_SRC": str(hermes),
        "SkipSuite": SkipSuite,
    }
    try:
        exec(compile(path.read_text(), str(path), "exec"), namespace)   # noqa: S102
    except SkipSuite as exc:
        print(f"  {YELLOW}SKIP{RESET}  {exc}", flush=True)
        return {"passed": 0, "skipped": declared, "failed": 0, "failures": [], "declared": declared}
    except ImportError as exc:
        if not harness.results:                    # missing dependency, not a broken assertion
            print(f"  {YELLOW}SKIP{RESET}  {exc}", flush=True)
            return {"passed": 0, "skipped": declared, "failed": 0, "failures": [], "declared": declared}
        print(f"  {RED}FAIL{RESET}  ImportError after {len(harness.results)} checks: {exc}", flush=True)
    except Exception as exc:                       # noqa: BLE001
        import traceback
        traceback.print_exc()
        print(f"  {RED}FAIL{RESET}  module aborted: {exc!r}", flush=True)

    passed = sum(1 for _n, ok, _d in harness.results if ok)
    failures = [n for n, ok, _d in harness.results if not ok]
    # The declared count is the module's contract with this runner: drift is a failure, never a
    # silent change to the totals.
    if len(harness.results) != declared:
        msg = f"module produced {len(harness.results)} checks, declared {declared}"
        print(f"  {RED}FAIL{RESET}  {msg}", flush=True)
        failures.append(msg)
    return {"passed": passed, "skipped": 0, "failed": len(failures),
            "failures": failures, "declared": declared}


def main() -> int:
    if len(sys.argv) == 3 and sys.argv[1] == "--module":
        path = HERE / sys.argv[2]
        result = run_one(path)
        print(RESULT_PREFIX + json.dumps(result), flush=True)
        return 1 if result["failed"] else 0

    hermes = find_hermes()
    print(f"{DIM}Hermes source: {hermes or 'not found'}{RESET}")

    totals = {"passed": 0, "skipped": 0, "failed": 0}
    all_failures: list[str] = []
    for name in MODULES:
        if not (HERE / name).is_file():
            raise SystemExit(f"missing test module: {HERE / name}")
        proc = subprocess.run(                                   # noqa: S603
            [sys.executable, str(Path(__file__).resolve()), "--module", name],
            cwd=str(REPO), capture_output=True, text=True, timeout=900,
        )
        body, result = proc.stdout, None
        for line in proc.stdout.splitlines():
            if line.startswith(RESULT_PREFIX):
                result = json.loads(line[len(RESULT_PREFIX):])
                body = body.replace(line + "\n", "")
        print(body.rstrip())
        if proc.stderr.strip():
            print(proc.stderr.rstrip())
        if result is None:
            print(f"  {RED}FAIL{RESET}  module produced no result line (exit {proc.returncode})")
            totals["failed"] += 1
            all_failures.append(f"{name}: no result")
            continue
        for key in totals:
            totals[key] += result[key]
        all_failures += [f"{name}: {f}" for f in result["failures"]]

    total = sum(totals.values())
    colour = RED if totals["failed"] else GREEN
    print("\n" + "=" * 62)
    print(f"  {GREEN}{totals['passed']} passed{RESET}   {YELLOW}{totals['skipped']} skipped{RESET}   "
          f"{colour}{totals['failed']} failed{RESET}   ({total} checks)")
    print("=" * 62)
    if all_failures:
        print("\nFailures:")
        for item in all_failures:
            print(f"  - {item}")
    if totals["skipped"]:
        print("\nTo run the skipped modules, point the runner at a Hermes checkout:\n"
              "  HERMES_AGENT_PATH=/path/to/hermes-agent python3 tests/hermes-plugin/run_tests.py")
    return 1 if totals["failed"] else 0


if __name__ == "__main__":
    sys.exit(main())
