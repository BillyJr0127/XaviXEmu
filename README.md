# XaviXEmu

[![Windows CI](https://github.com/BillyJr0127/XaviXEmu/actions/workflows/windows-ci.yml/badge.svg?branch=main)](https://github.com/BillyJr0127/XaviXEmu/actions/workflows/windows-ci.yml)

XaviXEmu is an experimental, open-source Windows emulator for selected XaviX,
XaviX 2000, and XaviX 2 television games. The project focuses on documented
hardware behaviour, motion-input emulation, reproducible tests, and
preservation of research for systems whose original optical accessories are
difficult to use on modern displays.

The current public release is **v0.3.1**. The project is not a
general-purpose XaviX emulator and does not claim cycle-accurate optical
sensor emulation.

## What's new in v0.3.1

Compared with v0.3.0, this release makes the largest XaviX 2 advance so far:
Naruto and Blue Dragon are now classified **Playable**, with corrected game
timing, much cleaner music release/loop behaviour, F5/F7 runtime states,
improved motion controls, and major sprite, scaling, transparency, crop, and
layer fixes. XaviX 2 audio is useful and substantially improved, but envelopes,
filtering, and some music behaviour are still not hardware-perfect.

Version 0.3.1 also adds remappable dual analog-stick reflector controls for PS
and compatible Windows gamepads, an initial two-Wii-Remote IR path, Japanese
and French interfaces, per-game INI settings, display-sized F9/F10 AVI/MP4
recording, and the local `save`/`snap` directory layout. The Wii Remote path
has not been tested on physical Wii Remote hardware by the maintainer and
should be considered experimental while awaiting user reports.

The exact supported `ban_dbz` image identifies itself on screen as `体験版`
(trial/store-demo version) and enters random battles rather than the retail
story. XaviXEmu therefore documents it as a demo image; external databases,
including MAME, may currently list it under the retail title and should review
that naming. Dragon Ball polygon terrain, lighting, enemies, effects, and audio
remain experimental.

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
| XaviX 2000 | `ban_omt` | Playable | Maintainer-verified gameplay, optical input, EEPROM, and runtime states |
| XaviX 2000 | `ttv_lotr` | Playable | Maintainer-verified gameplay with synthetic CU5501 sword input |
| XaviX 2000 | `ttv_sw` | Playable | Maintainer-verified gameplay with synthetic CU5501A input |
| XaviX 2000 | `ttv_swj` | Playable | Maintainer-verified Japanese gameplay with synthetic CU5501A input |
| XaviX 2000 | `ttv_mx` | Experimental | Race reached; keyboard/mouse tilt, graduated PS/gamepad analogue tilt, accelerator/brake/pause, 24C04 persistence, and PCM output |
| XaviX 2000 | `tom_jump` | Experimental | Game-mode menu reached; keyboard/mouse tilt, graduated PS/gamepad analogue tilt, accelerator/brake/pause, 24C04 persistence, and PCM output |
| XaviX 2000 | `epo_sdb` | Experimental | Safety/title screens, four controller channels, two buttons, and 4 KiB parallel NVRAM |
| XaviX 2000 | `epo_ebox` | Experimental | EPOCH/title/character/level screens, digital controls, 4 KiB parallel NVRAM, and PCM output |
| XaviX 2000 | `epo_es2j` | Experimental | Safety/title screens, game-select menu, attract-mode football, plain digital input, and PCM output |
| XaviX 2000 | `epo_hamc` | Experimental | EPOCH and animated title screens, acquisition synchronization, and PCM output |
| XaviX 2000 | `tom_dpgm` | Experimental | Disney logo and heart/wand sensor tutorial, 24C08 persistence, and PCM output |
| XaviX 2000 | `epo_mini` | Experimental | Safety warning and animated title screens, 24C08 initialization, and PCM output |
| XaviX 2000 | `epo_bowl` | Experimental | Safety/title/menu screens, synthetic sensor acquisition, and 24C04 persistence |
| XaviX 2000 | `tak_chq` | Experimental | Animated attract/title/race screens, GUI wheel/accelerator/brake/command input, CPU-car corner fix, 24C04 persistence, and PCM output |
| XaviX 2000 | `epo_golf` | Experimental | EPOCH/title screens, synthetic club motion, caddy/confirm and model/back controls, 24C04 persistence, and runtime states |
| XaviX 2000 | `duelmast` | Not working | Base-station BIOS and scanner diagnostic boot; cabinet controls and persistence are wired, but the required external game cartridge is absent |
| XaviX | `rad_mtrk` | Experimental | Exact ROM recognition, stable frame execution, 20 Hz pulse-wheel input, throttle/reverse/nitro/horn controls, and graduated mouse/PS analogue steering |
| XaviX | `rad_snow` | Experimental | Exact ROM recognition, stable frame execution, board direction/go/select controls, and graduated mouse/PS analogue tilt |
| XaviX | `rad_ssx` | Experimental | Exact ROM recognition, stable frame execution, board direction/go/select controls, and graduated mouse/PS analogue tilt |
| XaviX | `rad_sbw` | Experimental | Exact ROM recognition, stable frame execution, board direction/go/select controls, and graduated mouse/PS analogue tilt |
| XaviX | `tak_gin` | Experimental | Exact 2 MiB ROM recognition, stable frame execution, and four-direction keyboard/mouse/PS analogue board control |
| XaviX | `tcarnavi` | Experimental | Exact ROM recognition, stable frame execution, analogue steering plus accelerator/brake/key/siren/reverse/lights/horn/wipers/menu controls |
| XaviX | `tomthr` | Experimental | Safety/title/mode-select screens, corrected ADC completion, verified horn-to-menu entry, and directional rescue vehicle plus horn/key/map/wipers/lights/microphone controls |
| XaviX | `epo_crok` | Experimental | Safety/title screens, dual motion channels, red button, 24C04 persistence, and runtime states |
| XaviX | `tak_zuba` | Experimental | Title screen, dual sword motion channels, separate triggers, 24C02 persistence, and runtime states |
| XaviX | `epo_hamd` | Experimental | Animated title, decoded dual wireless controllers, activity-menu entry, and menu confirmation |
| XaviX | `tvpc_dor` | Experimental | Title, main menu, mouse, cursor-key matrix, 24C16 EEPROM, and verified flight-game input |
| XaviX | `tvpc_ham` | Experimental | Title/main menu, 24C16 EEPROM initialization, independent persistence, and PCM output |
| XaviX | `tvpc_hk` | Experimental | Title/main menu, 24C16 EEPROM initialization, independent persistence, and PCM output |
| XaviX 2 | `ban_naru` | Playable | Maintainer-verified gameplay, corrected later-stage enemies/attacks, motion input, runtime states, and improved provisional PCM |
| XaviX 2 | `ban_bldj` | Playable | Maintainer-verified story/battle gameplay, corrected scaled characters and translucent layers, independent reflector attacks, runtime states, and improved provisional PCM |
| XaviX 2 | `ban_db2j` | Playable (known issues) | User-verified story/battle play and motion input; F7 can resume with broken graphics, and attack/hit layers, transparency, polygon materials, and provisional PCM remain incomplete |
| XaviX 2 | `ban_dbz` | Playable (known issues) | User-verified random battles in the exact store-demo image, receiver/optical selection, basic/two-hand/deflect inputs, corrected terrain material routing, fractional zoom, and provisional PCM; polygon lighting remains incomplete |
| XaviX 2 | `epo_dab2j` | Experimental | XaviX/EPOCH logos, safety warning, later book-screen rendering, isolated runtime state, and PCM output |
| XaviX 2 | `epo_dtcj` | Experimental | XaviX/EPOCH startup, complete Doraemon tutorial/attract rendering, native filtered head-tilt input, keyboard/mouse/PS analogue/Wii controls, bindable boost, isolated runtime state, 24C04 persistence, and PCM output |
| XaviX 2 | `epo_pabj` | Experimental | XaviX/EPOCH startup and complete Pooh name-entry rendering, isolated runtime state, and PCM output |
| XaviX 2 | `epo_ssk2` | Not working | Exact ROM recognition, isolated runtime state, and dedicated 24C04 persistence |
| XaviX 2 | `epo_sskj` | Not working | Exact ROM recognition, isolated runtime state, early CPU/audio activity, and dedicated 24C04 persistence |

The status terms and known limitations are defined in
[docs/compatibility.md](docs/compatibility.md). In particular, reaching a menu
does not mean that a game is fully playable.

## Features

- Native Win32 front end with Traditional Chinese, Japanese, French, and English
  interfaces.
- PCSX2-style game library: choose a ROM directory, recursively identify exact
  supported ZIPs, show formal title/year/platform/maker/filename metadata and
  green **Fully playable**, yellow **Initial support**, or red **Not working**
  status, then sort by any column. Double-click or press Enter to launch.
  Titles with implemented host controls offer a right-click game-specific
  settings page containing only their real named actions; boot-only titles do
  not show a misleading generic binding page. The first `shortname-*.png`
  screenshot becomes that game's thumbnail.
- Nearest-neighbour output, window scaling, maximized mode, optional 4:3
  presentation, and borderless fullscreen.
- Mouse-driven virtual optical input, dual analog-stick PS/gamepad input, and
  initial dual-Wii-Remote IR positioning with per-game action bindings, plus
  early wireless-controller and TV-PC keyboard models.
- 24C02, 24C04, 24C08, and 24C16 EEPROM models.
- Per-game EEPROM and runtime-state files for supported original XaviX and
  XaviX 2000 profiles.
- PNG screenshots and selectable MJPEG AVI or H.264/AAC MP4 recordings generated locally by the user.
- Strict ROM size, CRC32, and SHA-1 verification.
- ROM-independent automated CPU, video, audio, peripheral, persistence,
  screenshot, and state tests.

See [docs/controls.md](docs/controls.md) for the current input mappings and
[docs/architecture.md](docs/architecture.md) for the source layout.

## System requirements

- 64-bit Windows 10 or Windows 11.
- A mouse, compatible Windows gamepad, or paired Wii Remote setup for the
  applicable motion-input profiles. Wii pointing requires a sensor bar.
- A lawfully obtained, exactly matching ROM ZIP.

XaviXEmu currently depends only on Windows system components at runtime. The
public release is not digitally signed, so Windows may display a reputation
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
3. Choose **File > Set ROM directory...** (or the large folder button), then
   select any folder on any mounted drive containing your lawfully obtained
   ZIPs. XaviXEmu scans that folder and all of its subfolders and lists exact
   supported images.
4. Double-click a game, press Enter on the selected row, or use **File > Open
   ROM ZIP...** to open one file directly. Right-click a row to start it or
   open that title's control guide and independent button settings.
5. While a game is running, press **Escape** once to pause and show the exit
   confirmation. Press **Escape** again to return to the game library, or
   press **Enter** to continue playing.

Green titles are verified fully playable, yellow titles have initial support,
and red titles are recognized but not working. Statuses follow XaviXEmu's own
verified gameplay, save-state probes, and maintainer testing. The selected ROM
directory and sort column are remembered in `XaviXEmu.ini`.
Scanning is read-only: the emulator does not copy, rename, modify, or add files
inside the ROM directory.

## Saves, screenshots, and recordings

For supported original XaviX, XaviX 2000, and XaviX 2 profiles, EEPROM (where
present) and F5/F7 runtime states are stored under the `save` directory beside
the executable. Existing files from older releases remain readable and are
migrated after a successful load. F8 writes game-prefixed PNG files under `snap` (the first one is used as the game-library thumbnail); choose **View > Recording format** to select
MJPEG AVI or H.264/AAC MP4. F9 starts a fixed-size 60 FPS recording with
synchronized 48 kHz stereo sound and F10 stops it in the same `snap` directory. By default the recording uses the
current display size, includes the presented host cursor, and rescales later
guest 320x240/640x480 mode changes into that fixed recording frame. Uncheck
**Record at current display size** under **View** to record at the guest
resolution active when F9 is pressed. These files are runtime output and must
not be committed.
XaviX 2 states are independent per ROM and can be shared privately for focused
debugging without sharing the ROM itself.

`XaviXEmu.ini` is stored beside the executable. The interface can be selected
manually in Traditional Chinese, Japanese, French, or English, or left on
automatic: Traditional-Chinese Windows locales prefer Traditional Chinese,
Japanese locales prefer Japanese, French locales prefer French, and all other
or unknown locales prefer English. The selected 1x-4x window scale is also
remembered and reapplied after launching a title from the ROM library.

## Known limitations

- Optical input is a synthetic model based on observed firmware behaviour; it
  is not a cycle-accurate model of the original camera and reflector geometry.
- `ban_omt`, `ttv_lotr`, `ttv_sw`, and `ttv_swj` have been
  maintainer-verified as playable, but their synthetic optical controls are
  not hardware-perfect.
- XaviX 2 still has unknown CPU behaviour, incomplete GPU coverage,
  provisional audio envelopes/filtering, and incomplete gesture
  classification.
- Only the exact known images in `docs/rom-metadata.md` are accepted.
- The front end is currently Windows-only.

## Project history and development disclosure

XaviXEmu began as a local investigation of `drgqst` against MAME revision
`cf2d7a9be259552a3b493156e50e9149b6856448`. The standalone source was
developed before it had its own Git repository. Its first public commit was
an honest initial import of that pre-Git development snapshot; no backdated
or artificial commits were created. Subsequent development has continued
through public commits, CI runs, and tagged releases.

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

Users may also report compatibility and controller feedback through
[Billy Jr.'s Emulator World on Facebook](https://www.facebook.com/61579382638861/).

## License

New XaviXEmu code is available under the BSD 3-Clause License. MAME-derived
portions retain their original BSD-3-Clause attribution, and miniz is
MIT-licensed. See [LICENSE](LICENSE),
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md), and
[docs/legal.md](docs/legal.md). The file-by-file classification and fixed
upstream references are recorded in [docs/provenance.md](docs/provenance.md).
