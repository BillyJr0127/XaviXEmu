# XaviX 2 sound processor

## Scope and source confidence

This note records the patent and firmware evidence behind the independent
XaviX 2 PCM implementation.  The strongest primary source found so far is SSD
Company Limited's XaviX 2 **Direct memory access controller**, JP 4,625,929 B2
(US publication 2009/0259789 A1).  Its block diagram names the XaviX 2 SPU and
documents the same 64-channel design observed in the games.  SSD's older
**Sound processor**, US 7,561,931 B1, supplies useful waveform-loop and timer
ancestry, but predates XaviX 2 and is not a register map for it.

Primary sources:

- [JP 4,625,929 B2, Direct memory access controller](https://patents.google.com/patent/JP4625929B2/en)
- [US 2009/0259789 A1, English family publication](https://patents.google.com/patent/US20090259789A1/en)
- [US 7,561,931 B1, Sound processor](https://patents.google.com/patent/US7561931B1/en)
- [US 7,908,416 B2, Data processing apparatus and bus arbitration method](https://patents.google.com/patent/US7908416B2/en)
- [US 6,070,205, High speed processor](https://patents.google.com/patent/US6070205A/en)

The 2000 sound patent predates the known XaviX 2 games, so a claim from it
applies only where the later XaviX 2 patent, ROM layout, firmware, or MMIO
traces independently agree.  It is not being treated as a complete XaviX 2
register manual.

## Exact XaviX 2 SPU architecture

JP 4,625,929 B2 identifies the multimedia processor as a CPU, rendering
processor (RPU), sound processing unit (SPU), SPU local RAM, geometry engine,
Y-sort unit, main RAM, and audio DAC.  Its SPU description gives several exact
constraints for emulation:

- a maximum of 64 simultaneous playback channels;
- time-division multiplexing of waveform and envelope data for those channels;
- multiplication of envelope data by each channel's volume before amplitude
  data reaches the audio DAC;
- independent SPU DMA requests for waveform data and envelope data;
- SPU local RAM holding waveform/envelope addresses and pitch information;
- hardware-fixed DMA priority of SPU, then RPU, then CPU.

This is an unusually strong match for the games' 64 live 64-byte channel
descriptors and independently programmed pitch, stereo volume, and envelope
state.  The current emulator functionally fetches sample data without modeling
the patented main-RAM waveform/envelope buffers or their bus arbitration.
Those omissions can affect DMA contention and transient timing even when the
steady PCM pitch is correct.

The older sound patent puts an interrupt accumulator in its word 0B.  In the
XaviX 2 descriptor, the corresponding observed byte offset `+$16` is pitch.
That conflict is direct evidence that old descriptor offsets must not be copied
into XaviX 2.  Only behaviour independently confirmed in the later architecture
or game firmware is portable.

## Patent architecture matched by the games

| Patent behaviour | XaviX 2 evidence | Confidence |
| --- | --- | --- |
| Signed 8-bit PCM, with the pitch accumulator's high part selecting byte addresses | ROM samples are signed 8-bit data terminated by `$80`; firmware computes Q16 pitch from the programmed engine divider | High |
| First waveform array followed by a second waveform array; the first end code transfers to the second, and the second end code loops to its own head | Descriptor addresses at `+$02/+$06` and `+$0e/+$12` point to two complete, separately terminated arrays | High |
| Separate waveform pitch and envelope control | Pitch is confirmed at descriptor `+$16`; release firmware reads and rewrites `+$36`, but the complete XaviX 2 envelope mapping is not decoded | Partial |
| Independent L/R envelope data and channel volume are multiplied with the waveform through cascaded DACs | Stereo volume bytes at `+$32/+$33` are confirmed; the remaining multiplier/envelope fields are unresolved | Partial |
| Four independently timed sound interrupt sources whose requests are ORed | The patent establishes the capability; current games prove one 120 Hz IRQ 7 sequencer source, not all four XaviX 2 sources | Architectural lead only |
| Programmable mute gaps and staggered DAC timing between TDM channel slots | This prevents physical DAC crosstalk.  The digital mixer already keeps channels separate; no audible timing model is justified yet | Architectural lead only |

## Attack and sustain-loop verification

Naruto's title-state descriptors provide a direct byte-level match to claim 7
and figures 8-9 of US 7,561,931 B1:

| Voice family | First array | First terminator | Second array | Second terminator |
| --- | ---: | ---: | ---: | ---: |
| lead | `$7cdd80` | `$7cf67f` | `$7cf680` | `$7d0eff` |
| bass | `$7ad400` | `$7aef7f` | `$7aef80` | `$7af07f` |
| chord | `$79b280` | `$7a197f` | `$7a1980` | `$7a42ff` |

The second address is exactly one byte beyond the first `$80` end code because
it is the head of the sustain array, not merely an end boundary.  The earlier
primary-array restart hypothesis is superseded by this patent/ROM agreement.
Looping `$240|channel` voices now play the complete attack once, then repeat the
second array.  F7 loading also derives the loop head from the live guest
descriptor so older runtime states do not preserve the provisional primary
loop address.

A deterministic 10-second capture from the same Naruto F7 state remained free
of signed-16-bit clipping.  The maximum same-channel sample jump fell from
6,652 to 6,227, with no jumps above 12,000.  These metrics only guard against a
new discontinuity; listening against hardware remains necessary for timbre.

## Envelope and release findings

Firmware routine `$40055d42-$40055d89` performs the following observed
sequence before a note-release update:

1. read descriptor byte `+$36`;
2. multiply it by the scalar at low RAM `$1441`;
3. divide the result by 64 and write it back to `+$36`;
4. set `$ea1b` bit 0 and issue `$c0|channel`.

This proves that `+$36` participates in the hardware release/envelope path, but
does not yet prove whether it is an envelope level, rate, preset selector, or a
packed combination.  Values such as `$e1`, `$64`, `$f0`, `$0f`, `$fb`, and
`$ff` vary by instrument.  The current 16-video-frame linear release therefore
remains explicitly provisional.  Replacing it requires either the XaviX 2
envelope transfer function or matched hardware captures of isolated releases;
blindly interpreting the nibbles would risk making music less accurate.

Descriptor fields `+$1e/+$22` form plausible 24-bit addresses, but their ROM
targets contain repeated structured preset records rather than a simple byte
envelope.  They must be traced through the preset loader before being mapped.

## Timing, stutter, and remaining work

The games program the PCM engine from `$ea00/$ea05`: Naruto and Blue Dragon use
213,068 Hz; the Dragon Ball games use 186,434 Hz.  Pitch is Q16 and must remain
independent of the 120 Hz score sequencer, which is why multiplying every
channel by a global speed factor is incorrect.

The older patent's four interrupt sources are a useful next lead, but current
runtime evidence proves only the independent 120 Hz IRQ 7 source.  Additional
sources will be implemented only after their XaviX 2 registers and firmware
uses are traced.  In particular, a short-lived menu or card-game speed change
must be correlated with the XaviX 2 global SPU controls and DMA activity; the
older word-0B timing field cannot safely be overlaid on the XaviX 2 pitch field.
The Windows title-bar diagnostics expose audio drop and underrun counters;
nonzero underruns identify host rendering that failed to supply 48 kHz audio in
real time and should not be disguised as a PCM clock correction.

Next evidence targets:

- trace descriptor/preset writes that establish the `+$36` release behaviour;
- identify any separate left/right envelope pointers and envelope pitch;
- compare isolated one-shot, sustained, and released notes against direct
  hardware audio;
- correlate any additional sound timer registers with firmware IRQ handling;
- determine the real DAC reconstruction/filter response after the digital
  waveform and envelope model are correct.