from __future__ import annotations

import importlib.util
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "prg32_image_convert", ROOT / "tools" / "prg32_image_convert.py"
)
assert SPEC and SPEC.loader
IMAGE_CONVERT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(IMAGE_CONVERT)


class CompactImagePackingTests(unittest.TestCase):
    def test_packed_indices_are_most_significant_pixel_first(self) -> None:
        self.assertEqual(
            IMAGE_CONVERT.pack_indexed([0, 1, 2, 3, 3, 2, 1, 0], 2),
            [0x1B, 0xE4],
        )

    def test_bitplanes_are_plane_major_and_frame_local(self) -> None:
        self.assertEqual(
            IMAGE_CONVERT.pack_bitplanes([0, 1, 2, 3, 0, 1, 2, 3], 2),
            [0x55, 0x33],
        )

    def test_one_bit_padding_stays_in_the_last_byte(self) -> None:
        self.assertEqual(IMAGE_CONVERT.pack_indexed([1, 0, 1], 1), [0xA0])

    def test_four_bit_mode_packs_two_pixels_per_byte(self) -> None:
        self.assertEqual(IMAGE_CONVERT.pack_indexed([0, 15, 8, 1], 4),
                         [0x0F, 0x81])

    def test_four_bit_odd_pixel_count_is_padded_low(self) -> None:
        self.assertEqual(IMAGE_CONVERT.pack_indexed([1, 2, 3], 4),
                         [0x12, 0x30])

    def test_eight_bit_mode_packs_one_pixel_per_byte(self) -> None:
        self.assertEqual(IMAGE_CONVERT.pack_indexed([0, 127, 255], 8),
                         [0, 127, 255])

    def test_exact_palette_is_shared_and_first_seen(self) -> None:
        palette, indices = IMAGE_CONVERT.exact_palette_indices(
            [0xF800, 0x07E0, 0xF800, 0x001F], 16
        )
        self.assertEqual(palette, [0xF800, 0x07E0, 0x001F])
        self.assertEqual(indices, [0, 1, 0, 2])

    def test_exact_palette_refuses_lossy_quantization(self) -> None:
        with self.assertRaisesRegex(SystemExit, "more than 16 exact RGB565 colors"):
            IMAGE_CONVERT.exact_palette_indices(list(range(17)), 16)

    def test_exact_palette_accepts_16_and_256_color_limits(self) -> None:
        palette16, indices16 = IMAGE_CONVERT.exact_palette_indices(
            list(range(16)), 16
        )
        palette256, indices256 = IMAGE_CONVERT.exact_palette_indices(
            list(range(256)), 256
        )
        self.assertEqual((len(palette16), indices16[-1]), (16, 15))
        self.assertEqual((len(palette256), indices256[-1]), (256, 255))

    def test_exact_palette_rejects_257_colors(self) -> None:
        with self.assertRaisesRegex(SystemExit, "more than 256 exact RGB565 colors"):
            IMAGE_CONVERT.exact_palette_indices(list(range(257)), 256)

    def test_bitplane_rows_cover_one_through_four_planes(self) -> None:
        indices = list(range(8))
        expected = {
            1: [0x55],
            2: [0x55, 0x33],
            3: [0x55, 0x33, 0x0F],
            4: [0x55, 0x33, 0x0F, 0x00],
        }
        for planes, packed in expected.items():
            with self.subTest(planes=planes):
                self.assertEqual(
                    IMAGE_CONVERT.pack_bitplane_rows(indices, 8, 1, planes),
                    packed,
                )

    def test_bitplane_rows_pad_each_odd_width_row(self) -> None:
        self.assertEqual(
            IMAGE_CONVERT.pack_bitplane_rows([1, 0, 1, 0, 1, 0], 3, 2, 1),
            [0xA0, 0x40],
        )

    def test_bitplane_frames_have_independent_offsets(self) -> None:
        first = IMAGE_CONVERT.pack_bitplane_rows([0, 1, 2, 3], 2, 2, 2)
        second = IMAGE_CONVERT.pack_bitplane_rows([3, 2, 1, 0], 2, 2, 2)
        self.assertEqual(first, [0x40, 0x40, 0x00, 0xC0])
        self.assertEqual(second, [0x80, 0x80, 0xC0, 0x00])
        self.assertEqual(first + second, first + second)

    def test_compact_c_emits_runtime_descriptor(self) -> None:
        text = IMAGE_CONVERT.emit_compact_c(
            "hero", [0x1B], [0x0000, 0xFFFF, 0xF800, 0x07E0],
            2, 2, 1, 2, 0,
        )
        self.assertIn("const prg32_indexed_sprite_t hero", text)
        self.assertIn("#define HERO_SPRITE PRG32_SPRITE_INDEXED(&hero)", text)
        self.assertIn(".bits_per_pixel = 2", text)
        self.assertIn(".palette_count = 4", text)
        self.assertIn(".transparent_index = 0", text)

    def test_compact_assembly_emits_tagged_bitplane_alias(self) -> None:
        text = IMAGE_CONVERT.emit_compact_asm(
            "hero", [0x80], [0x0000, 0xFFFF], 1, 1, 1, 1, -1,
            planar=True,
        )
        self.assertIn(".equ HERO_SPRITE, hero + 3", text)
        self.assertIn(".byte 1, 0", text)
        self.assertIn(".half 65535", text)

    def test_compact_c_emits_tagged_bitplane_alias(self) -> None:
        text = IMAGE_CONVERT.emit_compact_c(
            "hero", [0x80], [0x0000, 0xFFFF], 1, 1, 1, 1, -1,
            planar=True,
        )
        self.assertIn("#define HERO_SPRITE PRG32_SPRITE_BITPLANES(&hero)", text)


if __name__ == "__main__":
    unittest.main()
