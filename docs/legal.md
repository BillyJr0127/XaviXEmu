# Legal and redistribution notes

This document describes the project's distribution policy. It is not legal
advice.

## Project code

New XaviXEmu code is released under the BSD 3-Clause License and is identified
as `Copyright (c) 2026 Billy Jr. and contributors`. This wording identifies the
current maintainer without treating the project name as a legal entity, while
allowing future contributors to retain rights in their contributions.

## MAME-derived behaviour

XaviXEmu does not embed or launch MAME. Hardware behaviour was studied from
and, where noted in source headers, ported from individual MAME files licensed
under BSD-3-Clause. The exact MAME revision, source paths, named copyright
holders, and license text are recorded in `THIRD_PARTY_NOTICES.md`.
The file-by-file relationship assessment and header policy are recorded in
`docs/provenance.md`.

The original MAME authors remain credited. XaviXEmu's first-party copyright
notice does not replace or supersede their notices.

## miniz

The bundled miniz 2.2.0 reader is MIT-licensed. Its license includes the
copyright notices for RAD Game Tools and Valve Software, Rich Geldreich and
Tenacious Software LLC, and Martin Raiber. The complete text is present both
in `third_party/miniz/LICENSE` and `THIRD_PARTY_NOTICES.md`.

## ROMs, firmware, saves, and captures

No ROM, BIOS, firmware image, EEPROM image, save state, memory dump, gameplay
screenshot, official artwork, logo, character image, box art, or extracted
game asset is distributed in this repository.

The loader contains only identification metadata: shortname, byte size,
CRC32, and SHA-1. These values let the emulator reject unknown images and do
not reproduce game data. Users must provide their own lawfully obtained game
image and comply with the laws applicable to them.

The emulator opens a selected ZIP file read-only. Runtime saves and screenshots
are user-generated local files and must not be submitted to this repository.

## Names and trademarks

Game and hardware names are used only to describe compatibility. XaviXEmu is
unofficial and is not sponsored, approved, endorsed by, or affiliated with
the relevant publishers, developers, hardware manufacturers, or MAMEdev. All
product names and trademarks belong to their respective owners.

The public source tree intentionally contains no application icon until an
original, redistributable design is available.

## AI-assisted development

Development has been assisted by OpenAI Codex under human direction, review,
and testing. OpenAI is not listed as a XaviXEmu copyright holder. This
acknowledgement does not change the licenses of MAME-derived or miniz code.
