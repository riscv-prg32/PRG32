from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CART_LOADER = ROOT / "components" / "prg32" / "prg32_cart.c"


def test_abi_table_cartridges_do_not_require_runtime_load_addr_match() -> None:
    text = CART_LOADER.read_text(encoding="utf-8")
    legacy_check = (
        "if (import_model == PRG32_IMPORT_MODEL_LEGACY_ABSOLUTE &&\n"
        "        h->load_addr != (uint32_t)(uintptr_t)prg32_cart_exec)"
    )

    assert legacy_check in text
    assert '"cartridge linked for a different runtime address"' not in text


def test_abi_table_entries_receive_runtime_abi_table_pointer() -> None:
    text = CART_LOADER.read_text(encoding="utf-8")

    assert "typedef void (*prg32_cart_abi_entry_t)(const prg32_abi_table_t *abi);" in text
    assert "((prg32_cart_abi_entry_t)entry)(&prg32_abi_table);" in text


def test_stored_cartridges_load_through_flash_mapping_without_heap_copy() -> None:
    text = CART_LOADER.read_text(encoding="utf-8")
    load_stored = text.split("int prg32_cart_load_stored(void) {", 1)[1].split("\n}\n", 1)[0]

    # The payload is copied from the flash mapping straight into prg32_cart_exec;
    # a heap copy of a 128 KiB image cannot fit next to the 128 KiB window.
    assert "esp_partition_mmap(" in load_stored
    assert "heap_caps_malloc" not in load_stored
    # AUDIO assets play from the mapping, so slot rewrites must release it first.
    assert text.count("release_image_map_for_slot(slot);") == 3


def test_http_upload_streams_to_flash() -> None:
    text = (ROOT / "components" / "prg32" / "prg32_http_games.c").read_text(encoding="utf-8")
    post_game = text.split("static esp_err_t post_game(httpd_req_t *req) {", 1)[1].split("\n}\n", 1)[0]

    assert "prg32_cart_stream_begin(" in post_game
    assert "prg32_cart_stream_end(" in post_game
    assert "heap_caps_malloc((size_t)req->content_len" not in post_game


def test_usb_protocol_task_and_rings_stay_small() -> None:
    text = (ROOT / "components" / "prg32" / "prg32_usb_protocol.c").read_text(encoding="utf-8")

    # Sized from the measured worst case; see docs/hardware/memory.md.
    assert "#define PROTOCOL_TASK_STACK 4608u" in text
    assert "#define PROTOCOL_TX_RING_BYTES 1024u" in text


def test_reply_frames_cannot_be_split_by_console_output() -> None:
    text = (ROOT / "components" / "prg32" / "prg32_usb_protocol.c").read_text(encoding="utf-8")

    # Console text goes through the same driver as protocol frames ...
    assert "usb_serial_jtag_vfs_use_driver();" in text
    assert "uart_vfs_dev_use_driver(UART_NUM_0);" in text
    # ... and each frame is written in one call, so logs land only between frames.
    send_frame = text.split("static void send_frame(", 1)[1].split("\n}\n", 1)[0]
    assert send_frame.count("transport_write_frame(") == 1
    assert "frame_size > PROTOCOL_TX_FRAME_MAX" in send_frame
