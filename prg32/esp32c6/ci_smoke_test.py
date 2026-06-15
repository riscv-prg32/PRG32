#!/usr/bin/env python3
import subprocess
import sys
from pathlib import Path
from prg32.utilities.logging import *
from prg32.utilities.env_variables import PRG32_ENTRY


def run_check(name, *cmd, cwd=None):
    try:
        subprocess.check_call(list(cmd), cwd=cwd)
        log_ok(name)
    except subprocess.CalledProcessError:
        die(name)


def main():
    root = Path(__file__).resolve().parents[2]
    log_info("[CI] Checking whitespace")
    run_check("Whitespace check", "git", "diff", "--check", cwd=str(root))

    log_info("[CI] Compiling Python tools")
    run_check(
        "Python compile",
        sys.executable,
        "-m",
        "py_compile",
        PRG32_ENTRY,
        "tools/prg32_score_server/app.py",
        "tools/prg32_metrics_server/app.py",
        "tools/prg32_metrics_server/export_run.py",
        "tools/prg32_metrics_server/report.py",
        "tools/prg32_metrics_paper.py",
        cwd=str(root),
    )

    log_info("[CI] Running unit tests")
    run_check("Unit tests", sys.executable, "-m", "unittest", "discover", "-s", "tests", cwd=str(root))

    log_info("[CI] Running host-only cartridge doctor")
    run_check("Host cartridge doctor", sys.executable, PRG32_ENTRY, "doctor", "--host-only", cwd=str(root))

    log_ok("[CI] Host smoke test passed")


if __name__ == "__main__":
    main()
