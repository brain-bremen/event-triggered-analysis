#!/usr/bin/env python3
"""Regenerate the screenshot placeholders under Docs/assets/screenshots/.

Every image the documentation references exists as a file, because mkdocs runs
with ``strict: true`` and a missing one fails the build. Until the real
screenshots are taken, those files are the grey placeholders this script writes.

Replace a placeholder by overwriting the PNG in place, keeping the file name —
nothing in the markdown then has to change. Delete a name from ``SHOTS`` below
once its real screenshot is committed, so a later run of this script cannot
overwrite it.

    python Docs/assets/make_placeholders.py

Pure standard library: zlib and struct are enough to write a PNG, and the
alternative is a Pillow dependency for a picture of a grey rectangle.
"""

from __future__ import annotations

import struct
import zlib
from pathlib import Path

# name -> (width, height, caption drawn into the file's metadata)
SHOTS: dict[str, tuple[int, int, str]] = {
    "signal-chain": (1200, 380, "A signal chain with a triggered plugin in it"),
    "triggers-window": (900, 520, "TRIGGERS: the trigger source table"),
    "monitor-window": (900, 520, "MONITOR: per-source counters"),
    "average-editor": (620, 180, "Triggered Average: the editor"),
    "average-canvas": (1200, 750, "Triggered Average: the canvas"),
    "power-editor": (620, 180, "Triggered Power: the editor"),
    "power-analysis": (760, 640, "Triggered Power: ANALYSIS"),
    "power-canvas-spectrogram": (1200, 750, "Triggered Power: spectrogram mode"),
    "power-canvas-spectrum": (1200, 750, "Triggered Power: spectrum mode"),
    "coherence-editor": (620, 180, "Triggered Coherence: the editor"),
    "coherence-pairs": (760, 520, "Triggered Coherence: PAIRS"),
    "coherence-canvas": (1200, 750, "Triggered Coherence: the canvas"),
    "rf-editor": (620, 180, "Receptive Field Bar Mapper: the editor"),
    "rf-analysis": (760, 700, "Receptive Field Bar Mapper: ANALYSIS"),
    "rf-directions": (900, 620, "Receptive Field Bar Mapper: DIRECTIONS..."),
    "rf-canvas-map": (1200, 750, "Receptive Field Bar Mapper: Map view"),
    "rf-canvas-traces": (1200, 750, "Receptive Field Bar Mapper: Traces view"),
    "session-directory": (760, 420, "A saved session directory"),
}


def _chunk(tag: bytes, payload: bytes) -> bytes:
    return (
        struct.pack(">I", len(payload))
        + tag
        + payload
        + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF)
    )


def write_placeholder(path: Path, width: int, height: int, caption: str) -> None:
    """Write a checker-striped grey PNG that reads as 'not a screenshot yet'."""
    rows = bytearray()

    for y in range(height):
        rows.append(0)  # filter type 0 (None) for this scanline

        for x in range(width):
            # Diagonal stripes, so a placeholder is never mistaken for a
            # screenshot of something that happens to be flat grey.
            on_stripe = ((x + y) // 12) % 2 == 0
            border = x < 2 or y < 2 or x >= width - 2 or y >= height - 2
            value = 0x9A if border else (0xDA if on_stripe else 0xCE)
            rows.extend((value, value, value))

    header = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)  # 8-bit RGB

    png = (
        b"\x89PNG\r\n\x1a\n"
        + _chunk(b"IHDR", header)
        + _chunk(b"tEXt", b"Description\x00" + caption.encode("latin-1", "replace"))
        + _chunk(b"IDAT", zlib.compress(bytes(rows), 9))
        + _chunk(b"IEND", b"")
    )

    path.write_bytes(png)


def main() -> None:
    directory = Path(__file__).resolve().parent / "screenshots"
    directory.mkdir(parents=True, exist_ok=True)

    for name, (width, height, caption) in SHOTS.items():
        write_placeholder(directory / f"{name}.png", width, height, caption)
        print(f"wrote {name}.png  ({width}x{height})  {caption}")


if __name__ == "__main__":
    main()
