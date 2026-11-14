#!/usr/bin/env python3
"""Generate the N64 hardware database from ares' cartridge rules.

Use --source with an offline mia/medium/nintendo-64.cpp checkout, or download
the pinned version. Pass --revision to update; --check verifies the header.
"""

import argparse
import hashlib
import json
from pathlib import Path
import re
from urllib.request import urlopen


REVISION = "433d07fbdb90b02e6a0947b9de0fe5670378c375"
SOURCE_PATH = "mia/medium/nintendo-64.cpp"
OUTPUT = Path(__file__).resolve().parents[1] / "src/main/rom_database_ares.h"
TIMING_TAGS = Path(__file__).with_name("rom_timing_tags.json")
LICENSE = """Copyright (c) 2004-2025 ares team, Near et al

SPDX-License-Identifier: BSD-3-Clause
See the repository LICENSE for the license terms."""

PREAMBLE = """#ifndef N64EMU_ROM_DATABASE_ARES_H
#define N64EMU_ROM_DATABASE_ARES_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "core/types.h"

/* Input is the original big-endian header, before the core swaps ROM words.
 * Four controller ports and 8 MiB RDRAM are fixed by device initialization.
 * This imports save types and accessory capabilities. SRAM banking, flash
 * models and per-port attachment selection remain device/frontend concerns. */
static inline void ares_rom_settings(const uint8_t* header, core_rom_settings* settings)
{
    const uint8_t region_code = header[0x3e];
    const uint8_t revision = header[0x3f];
    unsigned eeprom = 0, sram = 0, flash = 0;
    int cpak = 0, rpak = 0, tpak = 0;

"""

EPILOGUE = """
    /* libdragon / EverDrive extended header. The frontend still selects
     * attachments; capabilities are aggregated across all four ports. */
    if (header[0x3c] == 'E' && header[0x3d] == 'D') {
        unsigned port;
        int configured = 0;
        switch (revision >> 4) {
        case 1: eeprom = 512; break;
        case 2: eeprom = 2048; break;
        case 3: sram = 32768; break;
        case 4: sram = 98304; break;
        case 5: flash = 131072; break;
        case 6: sram = 131072; break;
        }
        for (port = 0; port < 4; ++port) {
            const uint8_t config = header[0x34 + port];
            configured |= config;
            if (config == 1) rpak = 1;
            if (config == 2) cpak = 1;
            if (config == 3) tpak = 1;
        }
        if (!configured) rpak = 1;
    }

    settings->savetype = eeprom == 512 ? SAVETYPE_EEPROM_4K :
        eeprom ? SAVETYPE_EEPROM_16K :
        sram ? SAVETYPE_SRAM :
        flash ? SAVETYPE_FLASH_RAM : SAVETYPE_NONE;
    settings->mempak = cpak;
    settings->rumble = rpak;
    settings->transferpak = tpak;
    settings->biopak = 0;
}

#endif
"""


def generate(source, revision):
    text = source.decode("utf-8").replace("\r\n", "\n")
    rules = text.split("  //512B EEPROM\n", 1)[1].split("  // Paks for the 4 ports", 1)[0]
    ids = re.findall(r'id == "([A-Z0-9]{3})"', rules)
    if len(set(ids)) < 300:
        raise ValueError("ares cartridge rules changed; review the importer")
    rules = re.sub(r'id == "([A-Z0-9]{3})"', r'memcmp(header + 0x3b, "\1", 3) == 0', rules)
    rules = re.sub(r'flashModel = "[^"]*";\s*', "", rules)
    rules = re.sub(r'rtc = true;\s*', "", rules)
    rules = re.sub(r'(\d+)_KiB', lambda match: str(int(match[1]) * 1024), rules)
    if re.search(r'\b(id|flashModel|rtc)\b', re.sub(r'//[^\n]*', '', rules)):
        raise ValueError("unconverted ares cartridge rule")
    rules = "\n".join("  " + line.rstrip() if line.strip() else "" for line in rules.splitlines()) + "\n"
    tags = json.loads(TIMING_TAGS.read_text())
    timing = "\n    /* Unknown titles use Compatible; known IDs default to Fast. Presence\n     * is independent of save hardware and region-specific rules. */\n"
    timing += "    static const char known_ids[][4] = {\n"
    known_ids = sorted(set(ids))
    for offset in range(0, len(known_ids), 8):
        timing += "        " + ", ".join(f'\"{game_id}\"' for game_id in known_ids[offset:offset + 8]) + ",\n"
    timing += "    };\n    settings->timing_compatible = 1;\n"
    timing += "    for (unsigned i = 0; i < sizeof(known_ids) / sizeof(known_ids[0]); ++i) {\n"
    timing += "        if (memcmp(header + 0x3b, known_ids[i], 3) == 0) {\n"
    timing += "            settings->timing_compatible = 0;\n            break;\n        }\n    }\n"
    timing += "    /* Explicit local timing annotations override the known-ID default. */\n"
    for game_id, entry in sorted(tags.items()):
        if game_id not in ids:
            raise ValueError(f"timing tag has no Ares database entry: {game_id}")
        if entry.get("timing") not in ("fast", "compatible"):
            raise ValueError(f"invalid timing tag for {game_id}")
        if entry["timing"] == "compatible":
            timing += f'    if (memcmp(header + 0x3b, "{game_id}", 3) == 0) settings->timing_compatible = 1;\n'
    epilogue = EPILOGUE.replace("    settings->biopak = 0;\n", "    settings->biopak = 0;\n" + timing)
    notice = (
        "/* Generated by tools/update_rom_database.py; do not edit.\n"
        f" * Source: https://github.com/ares-emulator/ares/blob/{revision}/{SOURCE_PATH}\n"
        f" * Source SHA-256: {hashlib.sha256(source).hexdigest()}\n"
        f" * Game IDs: {len(set(ids))}, plus the EverDrive extended header.\n"
        f" * Local timing tags: tools/rom_timing_tags.json (SHA-256 {hashlib.sha256(TIMING_TAGS.read_bytes()).hexdigest()})\n *\n"
        + "\n".join(" * " + line if line else " *" for line in LICENSE.splitlines())
        + "\n */\n\n"
    )
    return (notice + PREAMBLE + "    //512B EEPROM\n" + rules + epilogue).encode("utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path)
    parser.add_argument("--revision", default=REVISION)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    if not re.fullmatch(r"[0-9a-f]{40}", args.revision):
        parser.error("--revision must be a complete ares commit hash")
    if args.source:
        source = args.source.read_bytes()
    else:
        url = f"https://raw.githubusercontent.com/ares-emulator/ares/{args.revision}/{SOURCE_PATH}"
        with urlopen(url, timeout=30) as response:
            source = response.read()
    generated = generate(source, args.revision)
    if args.check:
        if OUTPUT.read_bytes() != generated:
            raise SystemExit("ares ROM database is out of date")
        print("ares ROM database matches the pinned source")
    else:
        OUTPUT.write_bytes(generated)
        print(f"Updated {OUTPUT}")


if __name__ == "__main__":
    main()
