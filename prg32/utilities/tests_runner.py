import subprocess
import sys
from pathlib import Path
from prg32.utilities.logging import log_info, log_error, log_ok, step, die

def run_tests(args) -> None:
    step("Running Python unit tests")
    
    env = {}
    import os
    env.update(os.environ)
    env["PYTHONPYCACHEPREFIX"] = "/tmp/prg32-pycache"
    
    try:
        subprocess.check_call(
            [sys.executable, "-m", "unittest", "discover", "-s", "tests"],
            env=env
        )
        log_ok("Unit tests passed")
    except subprocess.CalledProcessError:
        log_error("Unit tests failed")
        sys.exit(1)

    step("Running QEMU smoke test")
    smoke_test_path = Path("prg32/qemu/smoke_test.py")
    if smoke_test_path.exists():
        try:
            subprocess.check_call([sys.executable, str(smoke_test_path)])
            log_ok("Smoke test passed")
        except subprocess.CalledProcessError:
            log_error("Smoke test failed")
            sys.exit(1)
    else:
        log_error(f"Smoke test script not found at {smoke_test_path}")
        sys.exit(1)

    log_ok("All tests passed successfully.")
