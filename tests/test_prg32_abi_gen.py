from __future__ import annotations

import json
import re
import subprocess
import tempfile
from pathlib import Path
import unittest

from prg32.abi.abi_gen import PLACEHOLDER_NOTE, abi_hash, load_abi


ROOT = Path(__file__).resolve().parents[1]
ABI_JSON = ROOT / "prg32/abi/prg32_abi.json"
HEADERS = [ROOT / "components/prg32/include", ROOT / "components/prg32_audio/include"]


def header_prototypes() -> str:
    """All public headers as one line, without comments or preprocessor lines."""
    text = ""
    for folder in HEADERS:
        for header in sorted(folder.glob("*.h")):
            text += "\n" + header.read_text(encoding="utf-8")
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    text = re.sub(r"//[^\n]*", " ", text)
    text = re.sub(r"^\s*#[^\n]*", " ", text, flags=re.M)
    return re.sub(r"\s+", " ", text)


def normalize_pointer(text: str) -> str:
    return re.sub(r"\s*\*", " *", text.strip())


def parameter_type(parameter: str) -> str | None:
    """`const char *name` -> `const char *`; `void` -> None."""
    parameter = parameter.strip()
    if parameter in ("", "void"):
        return None
    match = re.match(r"^(.*[\s*])\w+$", parameter)
    return normalize_pointer(match.group(1) if match else parameter)


def declared_signature(source: str, name: str) -> tuple[str, list[str]] | None:
    match = re.search(
        r"(?:^|[;}{])\s*((?:[\w*]+\s+)*?[\w*]+\s*\**)\s*\b" + name + r"\s*\(([^;{]*?)\)\s*;",
        source,
    )
    if not match:
        return None
    args = [parameter_type(p) for p in match.group(2).split(",")]
    return normalize_pointer(match.group(1)), [a for a in args if a is not None]


class AbiGeneratorTests(unittest.TestCase):
    def test_generated_files_are_current(self) -> None:
        result = subprocess.run(
            ["python3", "-m", "prg32", "abi", "check"],
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_json_signatures_match_the_headers(self) -> None:
        # The JSON signatures feed the ABI hash, so they must describe the real
        # functions; before ABI 1.8 the first 124 entries were void(void).
        source = header_prototypes()
        for fn in json.loads(ABI_JSON.read_text(encoding="utf-8"))["functions"]:
            with self.subTest(function=fn["name"]):
                self.assertEqual(
                    declared_signature(source, fn["name"]),
                    (fn["return"], fn["args"]),
                )

    def test_every_function_has_a_real_note(self) -> None:
        abi = json.loads(ABI_JSON.read_text(encoding="utf-8"))
        abi["functions"][0]["notes"] = PLACEHOLDER_NOTE
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "abi.json"
            path.write_text(json.dumps(abi), encoding="utf-8")
            with self.assertRaises(SystemExit) as raised:
                load_abi(path)
        self.assertIn("prg32_ticks_ms", str(raised.exception))

    def test_axis_functions_are_appended_and_older_cartridges_stay_compatible(self) -> None:
        abi = load_abi(ABI_JSON)
        names = [fn["name"] for fn in abi["functions"]]
        self.assertEqual(
            names[140:144],
            ["prg32_input_axis_x", "prg32_input_axis_y",
             "prg32_input_axes_raw", "prg32_input_has_analog"],
        )
        self.assertEqual((abi["major"], abi["minor"]), (1, 8))
        # ABI 1.6 and 1.7 cartridges declare 0x260f6136 and 0xb3845925; their
        # 139 and 140 entries are unchanged prefixes of this table.
        self.assertEqual(names[139], "prg32_multiplayer_local_player_id")
        self.assertIn("0x260f6136", abi["compatible_hashes"])
        self.assertIn("0xb3845925", abi["compatible_hashes"])
        self.assertNotEqual(abi_hash(abi), 0x260F6136)


if __name__ == "__main__":
    unittest.main()
