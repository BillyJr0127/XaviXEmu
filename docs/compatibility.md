# Compatibility

Compatibility claims are intentionally conservative. A title is promoted only
when the stated milestone has been reproduced from an exact supported image.

## Status definitions

- **Working**: the title and its intended input, video, audio, and persistence
  have been extensively verified, with no known issue that materially affects
  normal play.
- **Playable**: normal gameplay is possible, but emulation or input accuracy
  still has known limitations and full hardware parity is not claimed.
- **Experimental**: a meaningful boot, menu, or gameplay milestone works, but
  major hardware paths or a complete play-through remain unverified.
- **Not working**: the title is recognized but cannot currently reach a useful
  interactive milestone.

No accepted title is classified **Working** in v0.3.0-alpha. No accepted title
is currently classified **Not working**; unknown ROMs are unsupported and are
rejected rather than assigned a compatibility status.

## Compatibility table

| Shortname | Platform | Status | What has been verified | Main limitations |
| --- | --- | --- | --- | --- |
| `drgqst` | XaviX 2000 | Playable | Boot, calibration, title, gameplay, narrow/broad/step-forward virtual sword input, 24C08 EEPROM, F5/F7 states | Synthetic CU5501A image; control feel and sound are not hardware-perfect |
| `ban_onep` | XaviX 2000 | Playable | Title, first battle, menu O/X, separate left/right reflectors, simultaneous double gesture, 24C04 EEPROM, F5/F7 states | Punch strength, later techniques, guard timing, and Zoro gesture feel need real-hardware calibration |
| `ban_omt` | XaviX 2000 | Playable | Maintainer-verified gameplay, direction selection, seal drawing, synthetic front/reverse reflector input, 24C04 EEPROM, F5/F7 states | Optical area and gesture thresholds are synthetic rather than calibrated original hardware |
| `ttv_lotr` | XaviX 2000 | Playable | Maintainer-verified gameplay using synthesized CU5501 sword input and 24C02 persistence | Synthetic sensor geometry; host crosshair is shown only when the game-owned cursor is absent |
| `ttv_sw` | XaviX 2000 | Playable | Maintainer-verified gameplay using synthesized CU5501A input and 24C02 persistence | Synthetic sensor geometry; host crosshair is shown only when the game-owned cursor is absent |
| `ttv_swj` | XaviX 2000 | Playable | Maintainer-verified Japanese gameplay using synthesized CU5501A input and 24C02 persistence | Synthetic sensor geometry; host crosshair is shown only when the game-owned cursor is absent |
| `ttv_mx` | XaviX 2000 | Experimental | Boots to title and career menu; digital tilt/accelerator input, dedicated 24C04 persistence, and nonzero PCM output are verified | Gameplay, controller feel, audio accuracy, and a complete play-through remain unverified |
| `tom_jump` | XaviX 2000 | Experimental | Boots to title and start menu; digital tilt/accelerator input, dedicated 24C04 persistence, and nonzero PCM output are verified | Gameplay, controller feel, audio accuracy, and a complete play-through remain unverified |
| `epo_sdb` | XaviX 2000 | Experimental | Exact image recognition, safety and title screens, four ANPORT channels, two buttons, dedicated 4 KiB parallel NVRAM, and F5/F7 durable-storage semantics are verified | GUI control mapping, gameplay, audio accuracy, and a complete play-through remain unverified |
| `epo_hamd` | XaviX | Experimental | Verified two-chip ROM assembly, boot, animated title, decoded left/right wireless shake packets, activity-menu entry, and digital menu confirmation | Shake intensity fields and a complete play-through remain unverified |
| `tvpc_dor` | XaviX | Experimental | Boot, title, main menu, cumulative mouse counters, mouse button, cursor-key matrix, 24C16 EEPROM, F5/F7 states, and the take-copter flight input | Most keyboard keys, later programs, and a complete play-through remain unverified |
| `ban_naru` | XaviX 2 | Experimental | Boot, title, menu hit testing, character selection, story/tutorial path, native 320x240 crop, provisional signed 8-bit PCM | Unknown opcode behaviour, incomplete gestures/GPU/audio, no EEPROM, no F5/F7 state |
| `ban_bldj` | XaviX 2 | Experimental | Exact image accepted; Japanese safety warning and title screen rendered through the normal GPU path | Input, gameplay, EEPROM, persistence, and the currently silent audio path are unverified |
| `ban_db2j` | XaviX 2 | Experimental | Exact image accepted; BANDAI, Japanese safety, and title/menu screens rendered after program/data RAM separation | Input, gameplay, EEPROM, unknown opcodes, persistence, and the currently silent audio path are unverified |
| `ban_dbz` | XaviX 2 | Experimental | Exact image accepted; BANDAI, Japanese safety, and title/menu screens rendered with non-zero provisional PCM output | Input, gameplay, EEPROM, unknown opcodes, exact audio, and persistence are unverified |

## Reporting results

Compatibility reports must identify the XaviXEmu version, Windows version,
ROM shortname, exact reproduction steps, and whether the problem happens from
a clean EEPROM. Do not upload ROMs, saves, screenshots containing game art,
memory dumps, or extracted assets.
