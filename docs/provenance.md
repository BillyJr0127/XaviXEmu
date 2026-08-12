# Source provenance

This document records the origin of every first-party code file in
XaviXEmu. It is a source-level copyright and provenance audit, not a statement
that hardware facts, register addresses, or compatibility metadata are
copyrightable.

The MAME comparison is fixed to revision
`cf2d7a9be259552a3b493156e50e9149b6856448`. All MAME files named below carry
an individual `BSD-3-Clause` marker at that revision.

## Relationship definitions

- **Independent implementation**: written for XaviXEmu without copying or
  translating MAME source structure or expression.
- **Behavioral reference**: independently written code whose hardware
  behavior, register interpretation, test expectation, or ROM-identification
  metadata was checked against MAME.
- **Adapted**: contains material implementation structure or algorithms
  transformed from MAME and combined with substantial XaviXEmu-specific work.
- **Ported**: a substantial MAME implementation or instruction description was
  translated to the standalone C or Python architecture.

Classification is conservative at file level: if a file contains a material
MAME-derived section, the whole file is listed as **Adapted** or **Ported** even
when it also contains substantial independent work.

## Adapted and ported files

| XaviXEmu file | Origin / reference | MAME source file | MAME copyright holder | MAME revision | Relationship | License | Notes |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `src/core/xavix_cpu.c` | 6502 execution, XaviX banking/stack rules, SSD 2000 opcodes and reset behavior | `src/devices/cpu/m6502/m6502.cpp`, `m6502.h`, `om6502.lst`, `xavix.cpp`, `xavix.h`, `oxavix.lst`, `xavix2000.cpp`, `xavix2000.h`, `oxavix2000.lst` | Olivier Galibert; David Haywood | `cf2d7a9be259552a3b493156e50e9149b6856448` | Ported | BSD-3-Clause | MAME framework code was removed, but instruction algorithms, cycle/bus order and XaviX extensions are a material port. |
| `src/core/xavix_cpu.h` | CPU state and standalone interface derived while porting the same cores | Same CPU files as `xavix_cpu.c` | Olivier Galibert; David Haywood | `cf2d7a9be259552a3b493156e50e9149b6856448` | Adapted | BSD-3-Clause | Host callback API is new; the modeled CPU state and behavior belong to the port. |
| `src/core/xavix_machine.c` | XaviX memory map, register behavior, I/O direction, ADC selection, ANPORT dispatch, IRQ, DMA, sprite-fragment RAM and TX array | `src/mame/tvgames/xavix.cpp`, `xavix.h`, `xavix_m.cpp`, `xavix_v.cpp`, `xavix_io.cpp`, `xavix_io.h`, `xavix_anport.cpp`, `xavix_anport.h`, `xavix_adc.cpp`, `xavix_adc.h`, `xavix_2000.cpp`, `xavix_2000.h` | David Haywood; Angelo Salese | `cf2d7a9be259552a3b493156e50e9149b6856448` | Adapted | BSD-3-Clause | Standalone bus dispatch and hooks are new; material maps and register algorithms are adapted. The EPO SDB ANPORT mapping and parallel-NVRAM window were checked against the same revision. Both holders named by the shared `xavix.cpp` source are retained conservatively. |
| `src/core/xavix_machine.h` | Standalone state assembled from XaviX driver/register state | Same machine sources as `xavix_machine.c` | David Haywood; Angelo Salese | `cf2d7a9be259552a3b493156e50e9149b6856448` | Adapted | BSD-3-Clause | Pointer-free host layout is new, but the modeled register/state organization accompanies the adapted machine implementation. |
| `src/core/xavix_video.c` | Palette conversion, tilemaps, sprites, bit-packed graphics, priority and clipping | `src/mame/tvgames/xavix_v.cpp`, `src/mame/tvgames/xavix.h` | David Haywood | `cf2d7a9be259552a3b493156e50e9149b6856448` | Ported | BSD-3-Clause | Includes a recognizable port of the Y/C/H palette formula and renderer algorithms. XaviXEmu sprite-watch reporting is new. |
| `src/core/xavix_video.h` | State and inputs needed by the ported renderer | `src/mame/tvgames/xavix_v.cpp`, `src/mame/tvgames/xavix.h` | David Haywood | `cf2d7a9be259552a3b493156e50e9149b6856448` | Adapted | BSD-3-Clause | Standalone API and reporting structures are new. |
| `src/core/xavix_audio.c` | Voice decoding, envelopes, tempo, timers, mixer and register semantics | `src/mame/tvgames/xavix_sound.cpp`, `xavix_sound.h`, `xavix_m.cpp`, `xavix.h` | ramacat; David Haywood | `cf2d7a9be259552a3b493156e50e9149b6856448` | Ported | BSD-3-Clause | Native engine behavior is materially ported; host-rate conversion and standalone buffering are XaviXEmu work. |
| `src/core/xavix_audio.h` | Standalone audio state corresponding to the ported voice engine | Same audio sources as `xavix_audio.c` | ramacat; David Haywood | `cf2d7a9be259552a3b493156e50e9149b6856448` | Adapted | BSD-3-Clause | MAME device/framework types are not present. |
| `src/core/xavix_peripherals.c` | 24Cxx protocol behavior, XaviX timer, math unit and general DMA | `src/devices/machine/i2cmem.cpp`, `i2cmem.h`; `src/mame/tvgames/xavix_math.cpp`, `xavix_math.h`, `xavix_m.cpp`, `xavix_2000.cpp` | smf; David Haywood | `cf2d7a9be259552a3b493156e50e9149b6856448` | Adapted | BSD-3-Clause | I2C, timer, math and DMA contain adapted behavior. CU5501/CU5501A sensor synthesis and portable serialization are independent XaviXEmu work within the same file. |
| `src/core/xavix_peripherals.h` | State and API for the adapted peripherals plus the new virtual optical sensor | Same peripheral sources as `xavix_peripherals.c` | smf; David Haywood | `cf2d7a9be259552a3b493156e50e9149b6856448` | Adapted | BSD-3-Clause | Conservative file-level classification because adapted and new state share this header. |
| `src/xavix2/xavix2_cpu.c` | XaviX 2 instruction decoder, execution semantics, flags and interrupt behavior | `src/devices/cpu/xavix2/xavix2.cpp`, `xavix2.h` | Olivier Galibert; Nathan Gilbert | `cf2d7a9be259552a3b493156e50e9149b6856448` | Ported | BSD-3-Clause | Material standalone C port of the MAME CPU core. |
| `src/xavix2/xavix2_cpu.h` | CPU state and callback interface created around the port | `src/devices/cpu/xavix2/xavix2.cpp`, `xavix2.h` | Olivier Galibert; Nathan Gilbert | `cf2d7a9be259552a3b493156e50e9149b6856448` | Adapted | BSD-3-Clause | Host interface is new; modeled state accompanies the ported core. |
| `src/xavix2/xavix2_machine.c` | XaviX 2 map, interrupts, PIO, DMA, palette and GPU descriptor renderer | `src/mame/tvgames/xavix2.cpp` | David Haywood | `cf2d7a9be259552a3b493156e50e9149b6856448` | Adapted | BSD-3-Clause | Register tracing, sensor experiments, event scheduling and the audio integration are XaviXEmu additions. CPU composition does not make the MAME CPU authors authors of this machine file. |
| `src/xavix2/xavix2_machine.h` | Standalone state for the adapted XaviX 2 driver | `src/mame/tvgames/xavix2.cpp` | David Haywood | `cf2d7a9be259552a3b493156e50e9149b6856448` | Adapted | BSD-3-Clause | Diagnostic state is new; driver-derived machine state remains attributed. |
| `tools/xavix2_disasm.py` | XaviX 2 instruction descriptions and formatting rules | `src/devices/cpu/xavix2/xavix2d.cpp`, `xavix2d.h` | Olivier Galibert; Nathan Gilbert | `cf2d7a9be259552a3b493156e50e9149b6856448` | Ported | BSD-3-Clause | Python command-line and ZIP handling are new; the decoder table/expression is a material port. |

## Independently written files with MAME as a behavioral reference

These files do not carry MAME authors in their copyright header because the
audit found no material MAME source expression in the file. The reference is
still recorded to distinguish clean implementation from no upstream influence.

| XaviXEmu file | Origin / reference | MAME source file | MAME copyright holder | MAME revision | Relationship | License | Notes |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `src/core/drgqst_core.c` | XaviXEmu integration and project firmware traces; MAME used for game profiles and device wiring | `src/mame/tvgames/xavix.cpp`, `xavix.h`, `xavix_2000.cpp`, `xavix_2000.h`, `xavix_anport.cpp`, `xavix_anport.h`, `xavix_adc.cpp`, `xavix_adc.h` | David Haywood; Angelo Salese | `cf2d7a9be259552a3b493156e50e9149b6856448` | Behavioral reference | BSD-3-Clause | Mouse gestures, boxing controls, optical exposure sequencing, EPO SDB host-input translation, EPO Boxing ANPORT/ADC isolation, EPO Bowling sensor synchronization, EPO ES2J plain-input/ANPORT/ADC isolation, EPO HAMC acquisition synchronization, TOM DPGM and EPO MINI 24C08/sensor isolation, and standalone synchronization are independently written. |
| `src/core/drgqst_core.h` | New standalone core API; profile identities checked against MAME | `src/mame/tvgames/xavix.cpp`, `xavix.h`, `xavix_2000.cpp`, `xavix_2000.h` | David Haywood; Angelo Salese | `cf2d7a9be259552a3b493156e50e9149b6856448` | Behavioral reference | BSD-3-Clause | No MAME framework interface is copied; the generic parallel-NVRAM, plain XaviX 2000, and dedicated EPO Bowling, EPO HAMC, and shared TOM DPGM/EPO MINI 24C08 synchronization profiles are based on board wiring and firmware experiments recorded under `docs/research`. |
| `src/rom_loader.c` | New miniz-based loader; supported-image identity metadata checked against MAME ROM declarations | `src/mame/tvgames/xavix.cpp`, `src/mame/tvgames/xavix_2000.cpp`, `src/mame/tvgames/xavix2.cpp` | David Haywood | `cf2d7a9be259552a3b493156e50e9149b6856448` | Behavioral reference | BSD-3-Clause | Only shortname, layout, size, CRC32 and SHA-1 metadata is retained. miniz is separately attributed under MIT. |
| `src/rom_loader.h` | New loader API; names and sizes correspond to the same compatibility metadata | `src/mame/tvgames/xavix.cpp`, `src/mame/tvgames/xavix_2000.cpp`, `src/mame/tvgames/xavix2.cpp` | David Haywood | `cf2d7a9be259552a3b493156e50e9149b6856448` | Behavioral reference | BSD-3-Clause | Contains no ROM data. |
| `tests/xavix_cpu_test.c` | New test harness and cases; expected CPU behavior checked against the MAME cores | Same MAME CPU sources listed for `src/core/xavix_cpu.c` | Olivier Galibert; David Haywood | `cf2d7a9be259552a3b493156e50e9149b6856448` | Behavioral reference | BSD-3-Clause | The prior merged author header was removed because MAME authors did not author this test file. |
| `tests/xavix_machine_test.c` | New tests of the standalone machine API | Same machine sources listed for `src/core/xavix_machine.c` | David Haywood; `xavix.cpp` also names Angelo Salese | `cf2d7a9be259552a3b493156e50e9149b6856448` | Behavioral reference | BSD-3-Clause | Test implementation is independent. |
| `tests/xavix_video_test.c` | New pixel/result tests around the standalone renderer | `src/mame/tvgames/xavix_v.cpp`, `xavix.h` | David Haywood | `cf2d7a9be259552a3b493156e50e9149b6856448` | Behavioral reference | BSD-3-Clause | Test implementation is independent. |
| `tests/xavix_audio_test.c` | New voice and timing tests around the standalone audio API | `src/mame/tvgames/xavix_sound.cpp`, `xavix_sound.h`, `xavix_m.cpp` | ramacat; David Haywood | `cf2d7a9be259552a3b493156e50e9149b6856448` | Behavioral reference | BSD-3-Clause | Test implementation is independent. |
| `tests/xavix_peripherals_test.c` | New protocol/register tests around XaviXEmu peripheral APIs | Same peripheral sources listed for `src/core/xavix_peripherals.c` | smf; David Haywood | `cf2d7a9be259552a3b493156e50e9149b6856448` | Behavioral reference | BSD-3-Clause | Sensor and state-format tests additionally cover independent XaviXEmu behavior. |
| `tests/xavix2_cpu_test.c` | New test harness; expected instruction behavior checked against the MAME core | `src/devices/cpu/xavix2/xavix2.cpp`, `xavix2.h` | Olivier Galibert; Nathan Gilbert | `cf2d7a9be259552a3b493156e50e9149b6856448` | Behavioral reference | BSD-3-Clause | Test implementation is independent. |
| `tests/xavix2_machine_test.c` | New tests for XaviXEmu's standalone XaviX 2 memory and interrupt model | `src/mame/tvgames/xavix2.cpp` | David Haywood | `cf2d7a9be259552a3b493156e50e9149b6856448` | Behavioral reference | BSD-3-Clause | Test implementation is independent; expected split-fetch and interrupt-delivery behavior is additionally grounded in firmware experiments documented in `docs/research/xavix2.md`. |
| `tests/xavix2_boot_probe.c` | New diagnostic/probing utility; map baseline checked against MAME and extended through firmware experiments | `src/mame/tvgames/xavix2.cpp` | David Haywood | `cf2d7a9be259552a3b493156e50e9149b6856448` | Behavioral reference | BSD-3-Clause | No ROM is embedded and no disassembly output is checked in. |

## Fully independent first-party code

| XaviXEmu file | Origin / reference | MAME source file | MAME copyright holder | MAME revision | Relationship | License | Notes |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `src/core/drgqst_state.c` | XaviXEmu portable save-state format | — | — | — | Independent implementation | BSD-3-Clause | Explicit versioned encoding; no MAME serialization code. |
| `src/core/drgqst_state.h` | XaviXEmu save-state API | — | — | — | Independent implementation | BSD-3-Clause | First-party interface. |
| `src/cursor_presentation.c` | XaviXEmu host cursor policy | — | — | — | Independent implementation | BSD-3-Clause | First-party UI logic. |
| `src/cursor_presentation.h` | XaviXEmu host cursor API | — | — | — | Independent implementation | BSD-3-Clause | First-party interface. |
| `src/main.c` | Win32 application, localization, input and presentation | — | — | — | Independent implementation | BSD-3-Clause | Uses documented Windows APIs and the XaviXEmu core. |
| `src/persistence.c` | Atomic EEPROM/runtime-state file handling | — | — | — | Independent implementation | BSD-3-Clause | First-party Windows persistence code. |
| `src/persistence.h` | Persistence API | — | — | — | Independent implementation | BSD-3-Clause | First-party interface. |
| `src/resource.h` | Win32 resource identifiers | — | — | — | Independent implementation | BSD-3-Clause | No icon or third-party artwork is present. |
| `src/screenshot.c` | WIC PNG capture | — | — | — | Independent implementation | BSD-3-Clause | First-party Windows integration. |
| `src/screenshot.h` | Screenshot API | — | — | — | Independent implementation | BSD-3-Clause | First-party interface. |
| `src/sha1.c` | SHA-1 implementation written for the ROM verifier from the published algorithm | — | — | — | Independent implementation | BSD-3-Clause | No third-party SHA-1 source was identified in the file or project history. |
| `src/sha1.h` | SHA-1 API | — | — | — | Independent implementation | BSD-3-Clause | First-party interface. |
| `src/win_audio.c` | WinMM output buffering | — | — | — | Independent implementation | BSD-3-Clause | First-party Windows integration. |
| `src/win_audio.h` | WinMM audio API | — | — | — | Independent implementation | BSD-3-Clause | First-party interface. |
| `src/xavix2/xavix2_audio.c` | Firmware traces and runtime experiments | — | — | — | Independent implementation | BSD-3-Clause | At the fixed MAME revision, `src/mame/tvgames/xavix2.cpp` explicitly leaves sound hardware unknown and contains no audio emulation to port. |
| `src/xavix2/xavix2_audio.h` | New experimental XaviX 2 audio API/state | — | — | — | Independent implementation | BSD-3-Clause | First-party interface. |
| `src/xavixemu.rc` | Win32 dialog, menus and version resources | — | — | — | Independent implementation | BSD-3-Clause | No copyrighted icon is included. |
| `tests/cursor_presentation_test.c` | XaviXEmu test suite | — | — | — | Independent implementation | BSD-3-Clause | Tests first-party UI behavior. |
| `tests/drgqst_boot_probe.c` | XaviXEmu diagnostic utility | — | — | — | Independent implementation | BSD-3-Clause | Operates on user-supplied ROM only at runtime. |
| `tests/drgqst_frame_probe.c` | XaviXEmu diagnostic utility | — | — | — | Independent implementation | BSD-3-Clause | No captured frames or ROM-derived output is stored in the source tree. |
| `tests/drgqst_state_test.c` | XaviXEmu test suite | — | — | — | Independent implementation | BSD-3-Clause | Tests the first-party save-state format. |
| `tests/persistence_test.c` | XaviXEmu test suite | — | — | — | Independent implementation | BSD-3-Clause | Tests first-party file handling. |
| `tests/rom_loader_metadata_test.c` | XaviXEmu test suite | — | — | — | Independent implementation | BSD-3-Clause | Verifies only public ROM-identification metadata and embeds no ROM data. |
| `tests/rom_verify.c` | XaviXEmu verifier utility | — | — | — | Independent implementation | BSD-3-Clause | Uses metadata exposed by `rom_loader`; no ROM is embedded. |
| `tests/screenshot_test.c` | XaviXEmu test suite | — | — | — | Independent implementation | BSD-3-Clause | Tests first-party WIC output. |
| `tests/sha1_test.c` | Published SHA-1 test vectors and XaviXEmu test harness | — | — | — | Independent implementation | BSD-3-Clause | Standard test vectors are facts, not copied library code. |
| `tests/win_audio_test.c` | XaviXEmu test suite | — | — | — | Independent implementation | BSD-3-Clause | Tests first-party buffering. |
| `tests/xavix2_audio_test.c` | XaviXEmu test suite | — | — | — | Independent implementation | BSD-3-Clause | Tests the independently developed experimental audio implementation. |

`CMakeLists.txt` and the repository-authored Markdown/text files are also
first-party work under BSD-3-Clause, but are build/documentation artifacts
rather than emulator source files. Vendored miniz files are third-party and
are documented separately in `THIRD_PARTY_NOTICES.md` and
`third_party/miniz/LICENSE`.

## File-header policy

Adapted or ported files use separate notices so authorship is not conflated:

```text
SPDX-License-Identifier: BSD-3-Clause
MAME-derived portions copyright-holder(s): <names from the upstream files>
XaviXEmu port/adaptation and modifications:
Copyright (c) 2026 Billy Jr. and contributors
```

Independent and behavioral-reference files use only the XaviXEmu copyright
line. Behavioral references are recorded in this document instead of implying
that a MAME contributor authored the independently written file.

## License preservation

The complete BSD 3-Clause conditions and disclaimer for the referenced MAME
files, the fixed revision, and every upstream copyright-holder line relied on
by this audit are reproduced in `THIRD_PARTY_NOTICES.md`. The repository's own
BSD 3-Clause terms are in `LICENSE`.

The complete miniz 2.2.0 MIT copyright and permission notice, including RAD
Game Tools and Valve Software, Rich Geldreich and Tenacious Software LLC, and
Martin Raiber, is preserved in both `THIRD_PARTY_NOTICES.md` and
`third_party/miniz/LICENSE`.
