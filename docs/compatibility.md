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

No accepted title is classified **Working** in v0.3.0-alpha. Exact supported
images that still fail to reach a useful interactive milestone are explicitly
listed as **Not working**; unknown ROMs are rejected rather than guessed.

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
| `epo_ebox` | XaviX 2000 | Experimental | Exact image recognition, EPOCH/title/character/level screens, digital select/back/direction input, open-bus ANPORT and unused ADC behavior, dedicated 4 KiB parallel NVRAM, F5/F7 durable-storage routing, and nonzero PCM output are verified | Gameplay, control feel, audio accuracy, and a complete play-through remain unverified |
| `epo_es2j` | XaviX 2000 | Experimental | Exact image recognition, safety and title screens, game-select menu, attract-mode football rendering, plain P0/P1 input, isolated ANPORT/ADC behavior, independent F5/F7 state routing, and nonzero PCM output are verified | Card-scanner protocol, host control mapping, interactive play, audio accuracy, and a complete play-through remain unverified |
| `epo_hamc` | XaviX 2000 | Experimental | Exact image recognition, EPOCH and animated title rendering, deterministic acquisition synchronization, neutral ADC input, independent F5/F7 state routing, and nonzero PCM output are verified | Physical glove protocol and geometry, non-neutral ADC data, host control mapping, gameplay, audio accuracy, and a complete play-through remain unverified |
| `tom_dpgm` | XaviX 2000 | Experimental | Exact image recognition, Disney logo and heart/wand sensor-tutorial rendering, deterministic acquisition synchronization, 24C08 EEPROM, independent F5/F7 state routing, and nonzero PCM output are verified | Optical geometry and timing, host control mapping, gameplay, audio accuracy, and a complete play-through remain unverified |
| `epo_mini` | XaviX 2000 | Experimental | Exact image recognition, safety-warning and animated-title rendering, 24C08 EEPROM initialization, independent F5/F7 state routing, and nonzero PCM output are verified without a timer bypass | Host control mapping, sensor behavior, gameplay, audio accuracy, and a complete play-through remain unverified |
| `epo_bowl` | XaviX 2000 | Experimental | Exact image recognition, safety/title/mode-menu rendering, synthetic sensor synchronization and ADC acquisition, IN0 menu input, dedicated 24C04 persistence, and F5/F7 state routing are verified | GUI control mapping, sensor timing and geometry, gameplay, audio accuracy, and a complete play-through remain unverified |
| `tak_chq` | XaviX 2000 | Experimental | Exact image recognition, animated attract/title/race rendering, P0 confirm/command probes, both player ANPORT reads, dedicated 24C04 persistence, and nonzero PCM output are verified | GUI controller mapping, sustained race play, the reported CPU-car corner failure, audio accuracy, and a complete play-through remain unverified |
| `epo_hamd` | XaviX | Experimental | Verified two-chip ROM assembly, boot, animated title, decoded left/right wireless shake packets, activity-menu entry, and digital menu confirmation | Shake intensity fields and a complete play-through remain unverified |
| `tvpc_dor` | XaviX | Experimental | Boot, title, main menu, cumulative mouse counters, mouse button, cursor-key matrix, 24C16 EEPROM, F5/F7 states, and the take-copter flight input | Most keyboard keys, later programs, and a complete play-through remain unverified |
| `tvpc_ham` | XaviX | Experimental | Exact image recognition, stable title/main-menu rendering, 24C16 EEPROM initialization, independent EEPROM/runtime-state routing, and nonzero PCM output | Host input mapping, keyboard-matrix semantics, later programs, audio accuracy, and a complete play-through remain unverified |
| `tvpc_hk` | XaviX | Experimental | Exact image recognition, stable title/main-menu rendering, 24C16 EEPROM initialization, independent EEPROM/runtime-state routing, and nonzero PCM output | Host input mapping, keyboard-matrix semantics, later programs, audio accuracy, and a complete play-through remain unverified |
| `ban_naru` | XaviX 2 | Experimental | Boot, title, menu hit testing, character selection, story/tutorial path, F5/F7 resume, second-stage enemy/projectile sprites and distance-based attacks, native 320x240 crop, and provisional signed 8-bit PCM are verified | Unknown opcode behaviour, later stages, incomplete gestures/GPU/audio, and a complete play-through remain unverified |
| `ban_bldj` | XaviX 2 | Experimental | Exact image accepted; title, mode selection, story, and battle render with fractional GPU scaling and translucent indexed-palette panels; the two-reflector confirm path, provisional PCM, and independent F5/F7 state are verified | Full gesture vocabulary, sustained gameplay, exact downscale sampling/audio, and EEPROM are unverified |
| `ban_db2j` | XaviX 2 | Experimental | Exact image accepted; title route selection, optical cursor/dwell menus, Shenron/Kame House story pages, mission selection, first-battle entry, per-title motion packets, provisional PCM, and independent F5/F7 state are verified | Polygon material/light commands `$0b/$0e` are incomplete, so battle terrain/effects are not yet rendered correctly; complete actions/play-through, EEPROM, and exact audio are unverified |
| `ban_dbz` | XaviX 2 | Experimental | Exact image accepted; receiver detection, optical cursor/dwell selection, real battle entry, basic/two-hand/deflect action paths, continuous fractional zoom, provisional PCM, and independent F5/F7 state are verified | Polygon material/light commands `$0b/$0e` are incomplete, so canyon/enemy/effect layers are not yet rendered correctly; the complete gesture vocabulary/play-through, EEPROM, and exact audio are unverified |
| `epo_dab2j` | XaviX 2 | Experimental | Exact image recognition, centered XaviX/EPOCH logos, Japanese safety warning, later book-screen rendering, nonzero PCM output, and isolated runtime-state routing are verified | The title and interactive input have not yet been reached; controller protocol, EEPROM durability, later video, and exact audio remain unverified |
| `epo_dtcj` | XaviX 2 | Experimental | Exact image recognition, startup logos, complete Doraemon tutorial rendering, nonzero PCM output, isolated runtime-state routing, and dedicated 512-byte 24C04 persistence are verified | Controller protocol, interactive play, later video, and exact audio remain unverified |
| `epo_pabj` | XaviX 2 | Experimental | Exact image recognition, startup logos, complete Pooh name-entry screen rendering, nonzero PCM output, and isolated runtime-state routing are verified | Name input, interactive play, EEPROM durability, later video, and exact audio remain unverified |
| `epo_ssk2` | XaviX 2 | Not working | Exact image recognition, isolated runtime-state routing, the board's 24C04-compatible PIO serial-bus route, and dedicated 512-byte persistence are implemented | The firmware still falls into an unresolved CPU/MMIO loop before producing a visible frame; title, controls, audio, and gameplay are not working |
| `epo_sskj` | XaviX 2 | Not working | Exact image recognition, isolated runtime-state routing, early CPU/audio activity, the board's 24C04-compatible PIO serial-bus route, and dedicated 512-byte persistence are implemented | Startup ends on a blank white frame, likely awaiting an unmodelled controller or hardware condition; title and gameplay are not working |

## Reporting results

Compatibility reports must identify the XaviXEmu version, Windows version,
ROM shortname, exact reproduction steps, and whether the problem happens from
a clean EEPROM. Do not upload ROMs, saves, screenshots containing game art,
memory dumps, or extracted assets.
