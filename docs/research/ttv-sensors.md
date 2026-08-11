# TTV CU5501 / CU5501A investigation

## Scope

- `ttv_lotr` (CU5501, 24C02)
- `ttv_sw` and `ttv_swj` (CU5501A, 24C02)
- ROM ZIPs were opened read-only and were not moved, renamed, modified, copied,
  committed, or included in a release archive.

## ROM and boot verification

- Observed symptom: all three games were rejected by the standalone loader.
- PC/address: normal post-boot idle loops settled near `$003c78` (`ttv_lotr`),
  `$00387f` (`ttv_sw`), and `$0037ea` (`ttv_swj`) after adding manifests.
- I/O/register: none; this was loader identification only.
- Hypothesis: the existing XaviX 2000 CPU/video core can boot the known images.
- Experiment: verify exact size, CRC32 and SHA-1, then run 1,200 frames.
- Result: all three reached their correct title screens without CPU stop.
- Code changed: strict manifests and independent save-file identities were added.

## Star Wars first blocking problem

- Observed symptom: the title displayed `SWING YOUR LIGHTSABER TO BEGIN`, but
  every mouse trajectory was ignored.
- PC/address: the acquisition code appears at ROM offsets around `$237980`;
  firmware writes ADC control `$40` at `$7b81` and reads the result at `$7b80`.
- I/O/register: each pass performs 31 rows of one discarded conversion plus 32
  stored pixels (1,023 ADC writes), synchronized by IN1 bits `$02/$04`.
- Hypothesis: TTV hardware does a dark-reference pass followed by a latched
  reflected-light exposure; treating every P1 write as a live lamp state erases
  the second image before it is read.
- Experiment: instrument pass lengths and non-zero pixels, then compare RAM with
  the virtual reflector held at host X=32 and X=223.
- Result: before correction, both positions produced identical RAM.  After
  latching the P1.5 exposure pulse until P1.0 begins readout, 44 RAM bytes differ,
  including the live `$3900` sensor buffer area; ordinary mouse swings enter the
  NEW GAME / RESUME GAME menu in both US and Japanese versions.
- Code changed: added an explicit CU5501A begin-scan operation and a TTV
  exposure-pending state.  No game-specific check is skipped.

## LOTR CU5501 difference

- Observed symptom: the CU5501A exposure model fixed Star Wars but left LOTR on
  `Swing Your Sword!`.
- PC/address: LOTR's sensor routine appears around ROM offset `$61800d`; it also
  performs two 1,023-conversion passes through `$7b81/$7b80`.
- I/O/register: unlike Star Wars, LOTR holds P1.5 high throughout the reflected
  pass and writes P1.0 low only after the scan.
- Hypothesis: CU5501 and CU5501A share geometry and synchronization but not the
  exposure-control sequence.
- Experiment: give LOTR the direct P1.5 illuminated-pass model while retaining
  the latched-pulse model for Star Wars.
- Result: reflected pixels reached the subtraction pass and mouse swings advanced
  from the title to the new-game save-area selection screen.
- Code changed: split the shared TTV profile into CU5501/24C02 and
  CU5501A/24C02 profiles.

## EEPROM and regression

- Observed symptom: these titles use a 24C02 rather than the existing 24C04 or
  24C08 models.
- I/O/register: I2C SCL/SDA remain on P1.4/P1.3; address space is 256 bytes with
  an 8-byte write page.
- Hypothesis: using a larger device can incorrectly acknowledge control bytes and
  wrap page writes at the wrong boundary.
- Experiment: add 24C02 control-address, page-wrap, sequential-read, and
  0xff-to-0x00 wrap tests.
- Result: all 11 automated suites pass; all six supported ROM manifests verify.
- Code changed: added the 24C02 bus profile and per-game EEPROM/runtime filenames.

## Current milestone

The first blocking hardware behavior is corrected for all three TTV games.
All three games reached their first menu milestones in the automated probes.
Subsequent user testing verified normal gameplay as playable with the
synthesized optical input.  The game-owned 2x2 cursor sprite families were
identified independently for LOTR, Star Wars US, and Star Wars Japan; the host
blue target is now drawn only on frames where those sprites are absent.
