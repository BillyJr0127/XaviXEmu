# Architecture

XaviXEmu is a small native Windows application with two isolated emulation
paths.

## Source layout

- `src/core`: XaviX 2000 CPU, machine bus, video, audio, EEPROM, mathematical
  peripherals, optical-input synthesis, and portable runtime state.
- `src/xavix2`: experimental XaviX 2 RISC CPU, machine, GPU command path, and
  PCM audio.
- `src`: Win32 front end, ZIP/ROM verification, persistence, screenshots,
  cursor presentation, and host audio.
- `tests`: ROM-independent automated tests plus optional diagnostic probes.
- `tools`: read-only reverse-engineering utilities.
- `docs/research`: dated observations, hypotheses, experiments, and results.
- `third_party/miniz`: the vendored MIT-licensed ZIP reader.

## XaviX 2000 path

The original XaviX and XaviX 2000 path is a compact interpreter and hardware
model for the exact code paths exercised by the accepted game images. It
includes 24C02, 24C04, 24C08, and 24C16 EEPROM behaviour and synthetic
CU5501/CU5501A-style optical images. It is not intended to replace the
general-purpose MAME framework.

## XaviX 2 path

The XaviX 2 path uses a separate RISC interpreter and memory map. It currently
exists to document and test verified behaviour for `ban_naru`; it must not be
described as a complete XaviX 2 implementation.

## Front end and persistence

The Win32 front end uses GDI for nearest-neighbour presentation, WinMM for
audio, WIC for PNG capture, and the common file dialog for selecting a ZIP.
ROM files are opened read-only. EEPROM and runtime-state files are written
beside the executable for supported XaviX 2000 profiles.

## Test boundaries

The CTest suite is ROM-independent. Boot probes and frame probes are built as
developer tools but are not registered as automated tests because they require
a lawfully obtained external ROM.
