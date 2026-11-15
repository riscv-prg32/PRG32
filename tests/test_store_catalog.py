"""The firmware's Cartridge Store catalog keeps ids and versions whole.

The setup screen reads the catalog in components/prg32/prg32_setup_store.c and
downloads with GET /api/games/<id>/download?architecture=...&version=... . A
fixed id buffer once cut a 49-character id to 47, and the store answered 400
"game not found". These checks keep the id, the version and the download URL
from being cut again. They read the source, because the file needs ESP-IDF.
"""

from __future__ import annotations

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "components" / "prg32" / "prg32_setup_store.c").read_text(encoding="utf-8")


def function_body(name: str) -> str:
    match = re.search(rf"^[a-z_].*\b{name}\(", SOURCE, re.MULTILINE)
    if match is None:
        raise AssertionError(f"function {name} not found")
    return SOURCE[match.start():SOURCE.index("\n}", match.end())]


class StoreCatalogTest(unittest.TestCase):
    def test_id_and_version_are_stored_whole(self) -> None:
        entry = SOURCE[SOURCE.index("typedef struct {"):SOURCE.index("} store_game_t;")]
        self.assertIn("char *id;", entry)
        self.assertIn("char *version;", entry)
        self.assertNotRegex(entry, r"char id\[")
        self.assertNotRegex(entry, r"char version\[")
        catalog = function_body("fetch_catalog")
        self.assertIn('g.id = json_string_dup(p, end, "id");', catalog)

    def test_latest_version_comes_first(self) -> None:
        catalog = function_body("fetch_catalog")
        self.assertLess(catalog.index('json_string_dup(p, end, "latest_version")'),
                        catalog.index('json_string_dup(p, end, "version")'))

    def test_the_download_url_is_sized_to_fit(self) -> None:
        download = function_body("stream_download")
        self.assertNotIn("char url[", download)
        self.assertIn("snprintf(NULL, 0, STORE_DOWNLOAD_URL_FORMAT", download)
        # The client copies the URL on init, so it is freed right after.
        self.assertLess(download.index("esp_http_client_init(&config);"),
                        download.index("heap_caps_free(url);"))

    def test_entry_strings_are_freed(self) -> None:
        release = function_body("store_game_release")
        self.assertIn("heap_caps_free(g->id);", release)
        self.assertIn("heap_caps_free(g->version);", release)
        self.assertIn("store_games_clear();", function_body("store_buffers_free"))
        # A new search empties the old list, freeing its strings, before reading.
        catalog = function_body("fetch_catalog")
        self.assertIn("store_games_clear();", catalog)
        self.assertNotIn("game_count = 0;", catalog)


if __name__ == "__main__":
    unittest.main()
