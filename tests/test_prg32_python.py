"""Tests for PRG32 Python (prg32/python): the language, the generators and the CLI.

The compiler is shared with PRG32-Construction-Kit; these tests check the SDK copy on its
own. Real cartridge builds run only where the RISC-V toolchain is installed.
"""

from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from prg32.python import asm_generator, compiler, generator

ROOT = Path(__file__).resolve().parents[1]
PONG = ROOT / "examples" / "games" / "pong" / "python" / "game.py"
TOOLCHAIN = shutil.which("riscv32-esp-elf-gcc") is not None

# Every prg32 statement and value except the AudioStudio names (sound_effect,
# soundtrack_play), which need a Construction Kit project.
EVERY_FUNCTION = '''"""Every PRG32 Python function, once."""
import prg32

x: int = 10
y: int = 20
alive: bool = True
bricks: list[int] = [0] * 8


def step(dx: int) -> None:
    global x
    x += dx


def twice(v: int) -> int:
    return v * 2


def init() -> None:
    prg32.splash("EVERY", "FUNCTION", 500, prg32.BLACK, prg32.WHITE, prg32.YELLOW)
    prg32.tile_define(1, ["XXXXXXXX", "X......X", "X......X", "X......X", "X......X", "X......X", "X......X", "XXXXXXXX"], prg32.GREEN, prg32.BLACK)
    prg32.playfield_clear(0, 0)
    prg32.playfield_put(0, 2, 3, 1)
    prg32.palette_set(1, prg32.RED)
    prg32.volume(200)
    prg32.mp_join()
    prg32.player_name()
    for i in range(8):
        bricks[i] = i * 3


def update() -> None:
    global y, alive
    if prg32.held(prg32.LEFT):
        step(-2)
    if prg32.pressed(prg32.A) and alive:
        prg32.note(60, 1, 200)
        prg32.beep(440, 50)
    if prg32.touching(x, y, 8, 8, 100, 100, 16, 16):
        alive = False
        prg32.score_submit(x)
        prg32.note_off(1)
        prg32.soundtrack_stop()
        prg32.sound_stop_all()
    y = prg32.clamp(y + prg32.sign(prg32.random(-1, 1)), 0, prg32.SCREEN_H - 8)
    y = prg32.max(prg32.min(y, 190), prg32.abs(-1)) + twice(0) + prg32.tile(0, 2, 3) * 0
    prg32.playfield_scroll(0, prg32.frame() % 8, 0)
    prg32.playfield_camera(0, 0)
    prg32.mp_share(x, y, 1)
    if prg32.peers() > 0:
        bricks[0] = prg32.peer_x(0) + prg32.peer_y(0) + prg32.peer_look(0)
    bricks[1] = -7 // 2 + -7 % 2 + (x << 1 >> 1 & 255 | 1 ^ 2) + ~0


def draw() -> None:
    prg32.clear(prg32.BLACK)
    prg32.playfield_draw(0, True)
    prg32.rect(x, y, 8, 8, prg32.WHITE)
    prg32.frame(0, 0, 320, 200, prg32.GRAY)
    prg32.rect_rgb(10, 10, 4, 4, prg32.rgb(255, 128, 0))
    prg32.pixel(5, 5, prg32.PINK)
    prg32.line(0, 0, 40, 40, prg32.LIME)
    prg32.circle(160, 100, 20, prg32.CYAN, True)
    prg32.bitmap(["...XX...", "..XXXX..", ".XXXXXX.", "XXXXXXXX", "XX.XX.XX", "XXXXXXXX", ".X....X.", "X......X"], 200, 20, prg32.YELLOW, 2)
    prg32.sprite8(["...XX...", "..XXXX..", ".XXXXXX.", "XXXXXXXX", "XX.XX.XX", "XXXXXXXX", ".X....X.", "X......X"], 220, 20, prg32.ORANGE, prg32.BLACK)
    prg32.text(8, 8, "SCORE", prg32.WHITE, prg32.BLACK)
    prg32.number(bricks[0] + prg32.sin(90) + prg32.cos(0), 64, 8, prg32.WHITE, prg32.BLACK)
    prg32.pixel_indexed(1, 1, 1)
    prg32.rect_indexed(2, 2, 2, 2, 1)
    prg32.band("LIVES", x)
    prg32.scoreboard("HIGH SCORES")
'''


def problems(source: str) -> list[dict]:
    return compiler.check(source, {"title": "Test"})


def wrap(body: str, header: str = "") -> str:
    """A whole game whose update() is `body`."""
    return (f"import prg32\n{header}\n\ndef init() -> None:\n    pass\n\n"
            f"def update() -> None:\n{body}\n\ndef draw() -> None:\n    pass\n")


class PythonLanguageTests(unittest.TestCase):
    def test_starter_and_pong_compile_to_the_shared_game_ir(self) -> None:
        for source in (compiler.STARTER, PONG.read_text(encoding="utf-8")):
            game_ir = compiler.python_to_ir(source, {"title": "Game"})
            self.assertEqual(game_ir["abi"], generator.IR_ABI)
            self.assertEqual(game_ir["source_mode"], "python")
            self.assertEqual(game_ir["python_language_version"], compiler.LANGUAGE_VERSION)

    def test_every_prg32_function_is_accepted_and_features_are_recorded(self) -> None:
        game_ir = compiler.python_to_ir(EVERY_FUNCTION, {"title": "Every"})
        self.assertIn("multiplayer", game_ir["required_features"])
        self.assertIn("tilemap", game_ir["required_features"])

    def test_output_is_deterministic(self) -> None:
        source = EVERY_FUNCTION
        first = generator.ir_to_c(compiler.python_to_ir(source, {"title": "Every"}))
        second = generator.ir_to_c(compiler.python_to_ir(source, {"title": "Every"}))
        self.assertEqual(first, second)
        self.assertEqual(asm_generator.ir_to_asm(compiler.python_to_ir(source, {"title": "Every"})),
                         asm_generator.ir_to_asm(compiler.python_to_ir(source, {"title": "Every"})))

    def test_generated_files_name_the_sdk(self) -> None:
        game_ir = compiler.python_to_ir(compiler.STARTER, {"title": "Starter"})
        self.assertIn(f"Generated by {generator.GENERATED_BY} from the saved PRG32 Python",
                      generator.ir_to_c(game_ir))
        self.assertIn(f"generated by {generator.GENERATED_BY} from the saved PRG32 Python",
                      asm_generator.ir_to_asm(game_ir))

    def test_python_outside_the_language_is_refused_with_its_line(self) -> None:
        cases = {
            "    while True:\n        pass": "statement",
            "    print(1)": None,
            "    x = 1.5": None,
            "    y: int = 7 / 2": None,
        }
        for body, code in cases.items():
            with self.subTest(body=body):
                found = problems(wrap(body))
                self.assertEqual(len(found), 1)
                self.assertGreaterEqual(found[0]["line"], 7)
                if code:
                    self.assertEqual(found[0]["code"], code)
        self.assertEqual(problems("import os\n")[0]["line"], 1)
        missing = problems("import prg32\n\ndef init() -> None:\n    pass\n")
        self.assertEqual(missing[0]["code"], "lifecycle_missing")

    def test_recursion_is_refused(self) -> None:
        source = wrap("    loop(1)", "\n\ndef loop(n: int) -> None:\n    loop(n)")
        self.assertEqual(len(problems(source)), 1)

    def test_size_limits(self) -> None:
        too_long = "import prg32\n" + "#\n" * compiler.MAX_LINES
        self.assertEqual(len(problems(too_long)), 1)
        too_deep = wrap("    x: int = " + "(" * 40 + "1" + ")" * 40)
        self.assertEqual(len(problems(too_deep)), 1)

    def test_division_follows_the_risc_v_processor(self) -> None:
        self.assertEqual(compiler._c_div(-7, 2), -3)
        self.assertEqual(compiler._c_mod(-7, 2), -1)
        self.assertEqual(compiler._c_div(5, 0), -1)
        self.assertEqual(compiler._c_mod(5, 0), 5)

    def test_the_program_is_never_imported_or_run(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            marker = Path(tmp) / "ran"
            source = wrap("    pass", f"\nopen({str(marker)!r}, 'w')")
            self.assertEqual(len(problems(source)), 1)
            self.assertFalse(marker.exists())
            self.assertNotIn("game", sys.modules)


class PythonCliTests(unittest.TestCase):
    def run_cli(self, *args: str) -> subprocess.CompletedProcess:
        env = {**os.environ, "PYTHONPATH": str(ROOT)}
        return subprocess.run([sys.executable, "-m", "prg32", "python", *args], cwd=ROOT, env=env,
                              capture_output=True, text=True)

    def test_new_check_and_translate(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            game = Path(tmp) / "game.py"
            self.assertEqual(self.run_cli("new", str(game)).returncode, 0)
            self.assertEqual(game.read_text(encoding="utf-8"), compiler.STARTER)
            self.assertNotEqual(self.run_cli("new", str(game)).returncode, 0)
            self.assertEqual(self.run_cli("check", str(game)).returncode, 0)
            out_c = Path(tmp) / "game.c"
            self.assertEqual(self.run_cli("to-c", str(game), "--entry-prefix", "starter",
                                          "--out", str(out_c)).returncode, 0)
            self.assertIn("void starter_update(void)", out_c.read_text(encoding="utf-8"))
            out_s = Path(tmp) / "game.S"
            self.assertEqual(self.run_cli("to-asm", str(game), "--out", str(out_s)).returncode, 0)
            self.assertIn(".globl", out_s.read_text(encoding="utf-8"))

    def test_check_reports_problems_as_json(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            game = Path(tmp) / "game.py"
            game.write_text(wrap("    while True:\n        pass"), encoding="utf-8")
            result = self.run_cli("check", str(game), "--json")
            self.assertEqual(result.returncode, 1)
            found = json.loads(result.stdout)
            self.assertEqual(found[0]["code"], "statement")
            self.assertEqual(found[0]["line"], 8)


KIT = Path(os.environ.get("PRG32_CONSTRUCTION_KIT", ROOT.parent / "PRG32-Construction-Kit"))


@unittest.skipUnless((KIT / "prg32_construction_kit" / "python_project.py").is_file(),
                     "needs a PRG32-Construction-Kit checkout (set PRG32_CONSTRUCTION_KIT)")
class ConstructionKitParityTests(unittest.TestCase):
    """The SDK and the kit turn the same game.py into the same C and assembly."""

    def kit_output(self, source: str) -> tuple[str, str]:
        script = (
            # Load the kit's compiler modules without its package __init__ (which needs Flask).
            "import json, sys, types\n"
            "package = types.ModuleType('prg32_construction_kit')\n"
            "package.__path__ = ['prg32_construction_kit']\n"
            "sys.modules['prg32_construction_kit'] = package\n"
            "from prg32_construction_kit import python_project, generator, asm_generator\n"
            "source = sys.stdin.read()\n"
            "game_ir = python_project.python_to_ir(source, {'title': 'Parity'})\n"
            "print(json.dumps([generator.ir_to_c(game_ir), asm_generator.ir_to_asm(game_ir)]))\n"
        )
        result = subprocess.run([sys.executable, "-c", script], cwd=KIT, input=source, capture_output=True, text=True,
                                env={**os.environ, "PYTHONPATH": str(KIT)})
        if result.returncode != 0:
            self.skipTest(f"the kit could not be imported: {result.stderr.strip().splitlines()[-1:]}")
        c_text, asm_text = json.loads(result.stdout)
        return (c_text.replace("PRG32-Construction-Kit", generator.GENERATED_BY),
                asm_text.replace("PRG32-Construction-Kit", generator.GENERATED_BY))

    def test_same_c_and_assembly_as_the_kit(self) -> None:
        for source in (compiler.STARTER, PONG.read_text(encoding="utf-8"), EVERY_FUNCTION):
            with self.subTest(source=source.splitlines()[0]):
                game_ir = compiler.python_to_ir(source, {"title": "Parity"})
                self.assertEqual(self.kit_output(source),
                                 (generator.ir_to_c(game_ir), asm_generator.ir_to_asm(game_ir)))


@unittest.skipUnless(TOOLCHAIN, "needs the RISC-V toolchain (source $HOME/esp-idf/export.sh)")
class PythonCartridgeBuildTests(unittest.TestCase):
    def build(self, source: Path, out: Path, *extra: str) -> None:
        env = {**os.environ, "PYTHONPATH": str(ROOT)}
        result = subprocess.run([sys.executable, "-m", "prg32", "python", "build", str(source), "--out", str(out),
                                 "--name", "test", "--entry-prefix", "test_game", *extra],
                                cwd=ROOT, env=env, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertGreater(out.stat().st_size, 100)

    def test_every_function_builds_from_c_and_from_assembly(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            game = Path(tmp) / "game.py"
            game.write_text(EVERY_FUNCTION, encoding="utf-8")
            self.build(game, Path(tmp) / "every.prg32")
            self.assertTrue((Path(tmp) / "every.c").is_file())
            self.build(game, Path(tmp) / "every-asm.prg32", "--asm")
            self.assertTrue((Path(tmp) / "every-asm.S").is_file())

    def test_pong_builds(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            self.build(PONG, Path(tmp) / "pong-python.prg32")


if __name__ == "__main__":
    unittest.main()
