# XaviXEmu

[![Windows CI](https://github.com/BillyJr0127/XaviXEmu/actions/workflows/windows-ci.yml/badge.svg?branch=main)](https://github.com/BillyJr0127/XaviXEmu/actions/workflows/windows-ci.yml)

XaviXEmu is an experimental, open-source Windows emulator for selected XaviX
2000 and XaviX 2 television games. The project focuses on documented hardware
behaviour, motion-input emulation, reproducible tests, and preservation of
research for systems whose original optical accessories are difficult to use
on modern displays.

The first public release is **v0.1.0-alpha**. The project is not a
general-purpose XaviX emulator and does not claim cycle-accurate optical
sensor emulation.

## Important legal notice

**No ROM, BIOS, firmware, game asset, save file, screenshot, official artwork,
or application icon is included.** Users must provide their own lawfully
obtained ROM ZIP. XaviXEmu opens that file read-only and accepts only exact
images listed in [the ROM metadata](docs/rom-metadata.md).

Game and hardware names are used only to describe compatibility. XaviXEmu is
unofficial and is not sponsored, approved, or endorsed by the relevant rights
holders or MAMEdev. See [the legal notes](docs/legal.md).

## Current status

| Platform | Title shortname | Status | Verified milestone |
| --- | --- | --- | --- |
| XaviX 2000 | `drgqst` | Playable | Boots, gameplay, virtual sword, EEPROM, and runtime states |
| XaviX 2000 | `ban_onep` | Playable | Gameplay, dual-reflector input, menus, EEPROM, and runtime states |
| XaviX 2000 | `ban_omt` | Playable | User-verified gameplay, optical input, EEPROM, and runtime states |
| XaviX 2000 | `ttv_lotr` | Playable | User-verified gameplay with synthetic CU5501 sword input |
| XaviX 2000 | `ttv_sw` | Playable | User-verified gameplay with synthetic CU5501A input |
| XaviX 2000 | `ttv_swj` | Playable | User-verified Japanese gameplay with synthetic CU5501A input |
| XaviX 2 | `ban_naru` | Experimental | Title, menus, character selection, story path, and provisional PCM audio |

The status terms and known limitations are defined in
[docs/compatibility.md](docs/compatibility.md). In particular, reaching a menu
does not mean that a game is fully playable.

## Features

- Native Win32 front end with Traditional Chinese and English interfaces.
- Nearest-neighbour output, window scaling, maximized mode, optional 4:3
  presentation, and borderless fullscreen.
- Mouse-driven virtual optical input for sword, boxing, and motion profiles.
- 24C02, 24C04, and 24C08 EEPROM models.
- Per-game EEPROM and runtime-state files for supported XaviX 2000 profiles.
- PNG screenshots generated locally by the user.
- Strict ROM size, CRC32, and SHA-1 verification.
- ROM-independent automated CPU, video, audio, peripheral, persistence,
  screenshot, and state tests.

See [docs/controls.md](docs/controls.md) for the current input mappings and
[docs/architecture.md](docs/architecture.md) for the source layout.

## System requirements

- 64-bit Windows 10 or Windows 11.
- A mouse for the current motion-input profiles.
- A lawfully obtained, exactly matching ROM ZIP.

XaviXEmu currently depends only on Windows system components at runtime. The
public alpha is not digitally signed, so Windows may display a reputation
warning for a newly downloaded build.

## Building from source

The supported build configuration is CMake with MinGW-w64 GCC on 64-bit
Windows.

```powershell
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

The executable is produced as `build\XaviXEmu.exe`. The `build` directory and
all binary output are ignored by Git.

The source tree vendors miniz 2.2.0 for read-only ZIP access. No package
download is needed during configuration or compilation.

## Running

1. Build or obtain a clean XaviXEmu release.
2. Start `XaviXEmu.exe`.
3. Choose **File > Open ROM ZIP...** or the equivalent Traditional Chinese
   command.
4. Select a lawfully obtained ZIP matching the documented metadata.

The emulator does not scan the computer for games and does not copy, rename,
modify, or add files to the selected ZIP.

## Saves and screenshots

For supported XaviX 2000 profiles, EEPROM and F5/F7 runtime states are stored
beside the executable. F8 writes a PNG under a local `snap` directory. These
files are runtime output and must not be committed or attached to bug reports.

XaviX 2 EEPROM and runtime states are not implemented in v0.1.0-alpha.

## Known limitations

- Optical input is a synthetic model based on observed firmware behaviour; it
  is not a cycle-accurate model of the original camera and reflector geometry.
- `ban_omt`, `ttv_lotr`, `ttv_sw`, and `ttv_swj` are user-verified as
  playable, but their synthetic optical controls are not hardware-perfect.
- XaviX 2 still has unknown CPU behaviour, incomplete GPU coverage,
  provisional audio envelopes/filtering, incomplete gesture classification,
  and no persistence.
- Only the exact known images in `docs/rom-metadata.md` are accepted.
- The front end is currently Windows-only.

## Project history and development disclosure

XaviXEmu began as a local investigation of `drgqst` against MAME revision
`cf2d7a9be259552a3b493156e50e9149b6856448`. The standalone source was
developed before it had its own Git repository. Its first public history will
therefore be an honest initial import of that pre-Git development snapshot;
no backdated or artificial commits will be created.

Development has been assisted by OpenAI Codex under human direction, review,
and testing. OpenAI is acknowledged as a development tool, not named as a
project copyright holder.

The original investigation notes are preserved under
[docs/research](docs/research/README.md).

## Contributing and security

Contributions are welcome when they are based on reproducible observations
and contain no proprietary game data. Read [CONTRIBUTING.md](CONTRIBUTING.md)
before submitting code or reports. Security issues should follow
[SECURITY.md](SECURITY.md).

## License

New XaviXEmu code is available under the BSD 3-Clause License. MAME-derived
portions retain their original BSD-3-Clause attribution, and miniz is
MIT-licensed. See [LICENSE](LICENSE),
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md), and
[docs/legal.md](docs/legal.md). The file-by-file classification and fixed
upstream references are recorded in [docs/provenance.md](docs/provenance.md).
