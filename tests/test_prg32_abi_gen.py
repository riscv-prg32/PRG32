from __future__ import annotations

import subprocess
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class AbiGeneratorTests(unittest.TestCase):
    def test_generated_files_are_current(self) -> None:
        result = subprocess.run(
            ["python3", "tools/prg32_abi_gen.py", "--check"],
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
