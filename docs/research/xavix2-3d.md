# XaviX 2 geometry and rendering hardware

## Scope and confidence

This note replaces screen-shape guesses with a hardware model derived from
SSD's own patent publications and values written by the guest software. The
patents do not use the retail name "XaviX 2", so the identification is an
inference. It is nevertheless a strong one: the geometry-engine register
names, widths, order and I/O addresses match the live `$ffffe800-$ffffe864`
register block, while the rendering structures match the 128-bit polygon,
64-bit sprite and 32-bit texture-attribute records submitted by the games.

The resulting conclusion is that XaviX 2 has a genuine fixed-function 3D
pipeline. It is not a PlayStation GPU clone. The two systems share the general
period vocabulary of fixed-point matrix/lighting hardware and textured
triangles, but have materially different CPUs, memory systems and renderers.

## Silicon identity

The best public die identification currently available is MAME's observation
of the marking `SSD 2002-2004 NEC 800208-51`. The same source records a
RISC-like CPU which is not derived from the earlier XaviX 6502 core. Its CPU
implementation is little-endian, has eight general registers plus 64 high
registers, and uses variable-length one- to four-byte instructions. This is
not the PlayStation's MIPS CPU.

No primary public data sheet for `800208-51` has been found. `NEC` on the die
is evidence of NEC involvement in fabrication or the mask, but is not evidence
that the part is a standard NEC graphics processor. SSD's own patents are the
usable hardware documentation.

Sources:

- [MAME XaviX 2 driver and die marking](https://github.com/mamedev/mame/blob/master/src/mame/tvgames/xavix2.cpp)
- [MAME XaviX 2 CPU implementation](https://github.com/mamedev/mame/blob/master/src/devices/cpu/xavix2/xavix2.cpp)

## Multimedia processor blocks

SSD's rendering patent describes a single multimedia processor containing the
CPU, CPU local RAM, DMA controller, external-memory interface, main RAM and
arbiter, sound processing unit and local RAM, geometry engine (GE), Y sorting
unit (YSU), rendering processing unit (RPU), palette RAM, ADC, video/audio DACs
and external interface. This is the same broad arrangement observed by the
firmware.

The division of work is important:

| Stage | Hardware responsibility |
|---|---|
| GE | Affine transform, perspective projection, viewport transform, lighting, back-face/view-volume culling, perspective weights and polygon depth |
| YSU | Sort polygon and sprite records by their first visible scanline and depth |
| RPU | Merge polygon/sprite streams, slice them one scanline at a time, interpolate pixels, fetch/filter textures, blend colors and emit video |

The RPU generates the display in horizontal-line order and uses two single-line
buffers. It does not require a PlayStation-style full-frame VRAM image. This
explains why a host renderer that globally sorts whole objects can appear
correct for simple 2D scenes yet fail when 3D terrain, sprites and transparent
layers intersect across many scanlines.

Primary source: [SSD image-generating and texture-mapping patent,
US20090278845A1](https://patents.google.com/patent/US20090278845A1/en).

## Geometry Engine register map

Figure 11 of SSD's geometry patent matches the live I/O block:

| Address | Register | Width / meaning |
|---:|---|---|
| `$ffffe800-$ffffe83c` | `GR0-GR15` | sixteen 32-bit working registers |
| `$ffffe840` | `Distance` | 16-bit unsigned projection-plane distance |
| `$ffffe844` | `ZNear` | 16-bit unsigned near plane |
| `$ffffe846` | `ZFar` | 16-bit unsigned far plane |
| `$ffffe848` | `ViewportOffsetX` | 12-bit unsigned fixed-point offset |
| `$ffffe84a` | `ViewportOffsetY` | 12-bit unsigned fixed-point offset |
| `$ffffe84c` | `Ambient` | 5-bit ambient level |
| `$ffffe850-$ffffe854` | `LightVX/Y/Z` | three 16-bit light-vector components |
| `$ffffe856` | `DepthBias` | 3-bit depth exponent bias |
| `$ffffe858` | `GECommand` | 5-bit command plus remove-invalid and interrupt-enable flags |
| `$ffffe859` | `GEStatus` | 8-bit status |
| `$ffffe85a` | `RepeatCount` | 16-bit repeat count |
| `$ffffe85c` | `PolygonCount` | 16-bit result count |
| `$ffffe860` | `GESourceAddress` | 16-bit source address |
| `$ffffe862` | `GEDestinationAddress` | 16-bit destination address |
| `$ffffe864` | `GEBaseAddress` | 16-bit base address, aligned to 16 bytes |

This corrects a major emulator assumption: `$ffffe846=$7fff` is a `ZFar`
value, not a magic value that disables a near plane. The actual `ZNear` is at
`$ffffe844`.

The documented operations are `NOP`, `Affine`, `Pproj`, `View`, `Cull`,
`Trans`, `Dot`, `Bright-P`, `Ccalc`, `APV`, `CWD`, `TDBP`, `TD`,
`MatrixMulti` and `Sqrt`. Composite commands are defined as:

- `APV`: `Affine` + perspective projection + viewport transform.
- `CWD`: back-face/view-volume culling + perspective-weight calculation +
  polygon-depth calculation.
- `TDBP`: normal transform + dot product + polygon brightness.
- `TD`: normal transform + dot product.

`Pproj` and `View`, or their combined `APV` form, emit `Vector16` records and
set a clipping flag when Z is outside `ZNear..ZFar`, X is outside `0..2047`, or
Y is outside `0..1023`. `CWD` consumes those records through the polygon's
vertex indices. A clipped vertex makes the polygon invalid; depending on the
`InvalidStructureRemove` command flag it is either filled with the
`X=2047,Y=1023` sentinel or removed while the valid records are compacted.

For textured polygons, `CWD` calculates `Bw` and `Cw` for perspective-correct
mapping. It also sums the three view-space Z values, converts the result to a
12-bit float with a four-bit exponent and eight-bit mantissa, applies the
three-bit `DepthBias`, then adds the signed 12-bit initial polygon depth.
Depth is consequently a polygon attribute, not a host-side average or
per-pixel Z buffer.

Primary source: [SSD arithmetic processing unit patent,
JP2007128180A](https://patents.google.com/patent/JP2007128180A/en) (English WO
family: [WO2007052682A1](https://patents.google.com/patent/WO2007052682A1/en)).

## RPU polygon, sprite and texture path

SSD documents a 128-bit polygon structure, a 64-bit sprite structure and a
32-bit texture-attribute structure. Polygon and sprite prefetchers feed a
merge sorter. Objects that continue onto the next scanline are retained in a
recycle buffer; new objects are merged with that stream for each line. A depth
comparator draws the farther object first, allowing the later near or
translucent object to blend correctly.

The Y ordering rule is also explicit: ascending minimum Y, and for equal Y,
descending depth. Objects clipped above the viewport are treated as starting
on the first line. This is not equivalent to the emulator's current global
`qsort`, area heuristic and several manually separated polygon/sprite passes.

The texture patent documents two distinct 64-bit-word block layouts:

| `Bit` | Map 0 `w x h` | Map 1 `w x h` |
|---:|---:|---:|
| 0 | 64 x 1 | 8 x 8 |
| 1 | 32 x 1 | 8 x 4 |
| 2 | 21 x 1 | 7 x 3 |
| 3 | 16 x 1 | 4 x 4 |
| 4 | 12 x 1 | 4 x 3 |
| 5 | 10 x 1 | 5 x 2 |
| 6 | 9 x 1 | 3 x 3 |
| 7 | 8 x 1 | 4 x 2 |

The current sampler uses the Map 1 table for both formats. That is a direct,
title-independent cause of corrupted terrain/building textures whenever a
polygon selects Map 0. The patent additionally defines the folded lower-half
triangle, word-address and bit-address equations; these should become golden
unit-test vectors before the sampler is changed.

`Filter=0` selects four-texel bilinear filtering and `Filter=1` selects nearest
texel filtering. Perspective correction interpolates `u/z`, `v/z` and `1/z`
and divides at the pixel stage. These are more reasons not to substitute a
PlayStation renderer.

## Comparison with the original PlayStation

The similarities are architectural vocabulary only: both machines accelerate
fixed-point coordinate conversion, lighting and textured polygon drawing.

Sony's documented PlayStation arrangement couples a GTE coordinate
coprocessor to the main CPU, sends drawing-command packets to a separate GPU,
and draws into a 1 MiB 1024x512x16 frame buffer which also stores CLUT and
texture regions. XaviX 2 instead has a custom variable-length CPU, separate
GE/YSU/RPU units, shared-memory polygon and sprite arrays, and a two-line-buffer
scanline compositor. A PlayStation GPU core therefore cannot be reused as the
XaviX 2 renderer.

Primary Sony sources:

- [Sony image-processing system with CPU/GTE and packet GPU,
  JPWO1997032248A1](https://patents.google.com/patent/JPWO1997032248A1/en)
- [Sony GTE/GPU and 1 MiB frame-buffer description,
  EP1029568A2](https://patents.google.com/patent/EP1029568A2/en)

## Implementation status (2026-08-27)

The first hardware rewrite is now in place:

- The GE register map uses the documented `ZNear`, `ZFar`, viewport and depth-bias locations.
- `Vector10` bit order and scale, `Vector32` Z/Y/X memory order and eight-byte `Vector16` output are implemented from Figures 13-15.
- `Affine`, `Pproj`, `View`, combined `APV`, `Cull` and combined `CWD` are routed through their documented command numbers.
- CWD now consumes indexed `Vector16` vertices, applies clipping and face tests, compacts invalid records for command `$4d`, calculates `Bw/Cw`, encodes the 12-bit polygon depth and updates `PolygonCount`.
- Texture Map 0 and Map 1 use their separate patented block-size tables, with regression vectors for the folded triangle layout.
- The focused geometry tests and the complete 17-test suite pass.

A cold-boot DBZ run with the verified two-reflector packet reaches a random battle in about 4,000 frames, independently of legacy F7 state. The guest issues the complete `$10/A cold-boot DBZ run with the verified two-reflector packet reaches a random battle in about 4,000 frames, independently of legacy F7 state. The guest issues the complete `$10/$0c/$0f/$0b/$0e/$4d` geometry chain and supplies nine model batches every frame. At the current checkpoint every polygon is rejected because at least one indexed `Vector16` vertex carries `Clipping=1`; therefore the empty terrain is upstream of texture sampling and RPU composition, not missing ROM data.

The next accuracy target is the patent execution unit's exact 16-to-32-bit type conversion and multiply/shift/saturation sequence. Matrix elements deliberately retain a 32-bit sign/upper half while firmware changes their low word, so a plain signed-low-16 interpretation is not yet sufficient evidence. Once APV emits non-clipped polygons in the cold-boot fixture, the next structural task is the scanline YSU/RPU merge.c/A cold-boot DBZ run with the verified two-reflector packet reaches a random battle in about 4,000 frames, independently of legacy F7 state. The guest issues the complete `$10/$0c/$0f/$0b/$0e/$4d` geometry chain and supplies nine model batches every frame. At the current checkpoint every polygon is rejected because at least one indexed `Vector16` vertex carries `Clipping=1`; therefore the empty terrain is upstream of texture sampling and RPU composition, not missing ROM data.

The next accuracy target is the patent execution unit's exact 16-to-32-bit type conversion and multiply/shift/saturation sequence. Matrix elements deliberately retain a 32-bit sign/upper half while firmware changes their low word, so a plain signed-low-16 interpretation is not yet sufficient evidence. Once APV emits non-clipped polygons in the cold-boot fixture, the next structural task is the scanline YSU/RPU merge.f/A cold-boot DBZ run with the verified two-reflector packet reaches a random battle in about 4,000 frames, independently of legacy F7 state. The guest issues the complete `$10/$0c/$0f/$0b/$0e/$4d` geometry chain and supplies nine model batches every frame. At the current checkpoint every polygon is rejected because at least one indexed `Vector16` vertex carries `Clipping=1`; therefore the empty terrain is upstream of texture sampling and RPU composition, not missing ROM data.

The next accuracy target is the patent execution unit's exact 16-to-32-bit type conversion and multiply/shift/saturation sequence. Matrix elements deliberately retain a 32-bit sign/upper half while firmware changes their low word, so a plain signed-low-16 interpretation is not yet sufficient evidence. Once APV emits non-clipped polygons in the cold-boot fixture, the next structural task is the scanline YSU/RPU merge.b/A cold-boot DBZ run with the verified two-reflector packet reaches a random battle in about 4,000 frames, independently of legacy F7 state. The guest issues the complete `$10/$0c/$0f/$0b/$0e/$4d` geometry chain and supplies nine model batches every frame. At the current checkpoint every polygon is rejected because at least one indexed `Vector16` vertex carries `Clipping=1`; therefore the empty terrain is upstream of texture sampling and RPU composition, not missing ROM data.

The next accuracy target is the patent execution unit's exact 16-to-32-bit type conversion and multiply/shift/saturation sequence. Matrix elements deliberately retain a 32-bit sign/upper half while firmware changes their low word, so a plain signed-low-16 interpretation is not yet sufficient evidence. Once APV emits non-clipped polygons in the cold-boot fixture, the next structural task is the scanline YSU/RPU merge.e/$4d` geometry chain and supplies nine model batches every frame.

The battle trace exposed two format details that were previously conflated with Matrix32:

- MatrixMulti composes the source matrix on the left (`source * registers`). This keeps an object's world translation outside its scale matrix.
- Affine's nine 16-bit basis elements are signed Q7.8. The firmware writes them with halfword stores after shifting packed coefficients by eight; upper register halves left by MatrixMulti are unrelated. Multiplying Q7.8 by Vector10 Q0.9 produces 17 fractional bits, so Scale 0/1/2/3 shifts the product by -1/0/+1/+2 before adding the complete Q15.16 translation.

With those rules, the DBZ F7 battle fixture changes from zero accepted polygons to six non-empty model batches in its first frame (up to 180 polygons in one batch). The emulator now renders the ROM's own animated Frieza model, green ground, rock mesh and distant terrain, and all remain present after an eight-frame resume. This also proves that the battle data was valid 3D geometry rather than an absent 2D enemy layer.

The remaining visual differences are now downstream: several eye-plane/viewport-crossing batches still need exact clipping, and commands `A cold-boot DBZ run with the verified two-reflector packet reaches a random battle in about 4,000 frames, independently of legacy F7 state. The guest issues the complete `$10/$0c/$0f/$0b/$0e/$4d` geometry chain and supplies nine model batches every frame. At the current checkpoint every polygon is rejected because at least one indexed `Vector16` vertex carries `Clipping=1`; therefore the empty terrain is upstream of texture sampling and RPU composition, not missing ROM data.

The next accuracy target is the patent execution unit's exact 16-to-32-bit type conversion and multiply/shift/saturation sequence. Matrix elements deliberately retain a 32-bit sign/upper half while firmware changes their low word, so a plain signed-low-16 interpretation is not yet sufficient evidence. Once APV emits non-clipped polygons in the cold-boot fixture, the next structural task is the scanline YSU/RPU merge.b/A cold-boot DBZ run with the verified two-reflector packet reaches a random battle in about 4,000 frames, independently of legacy F7 state. The guest issues the complete `$10/$0c/$0f/$0b/$0e/$4d` geometry chain and supplies nine model batches every frame. At the current checkpoint every polygon is rejected because at least one indexed `Vector16` vertex carries `Clipping=1`; therefore the empty terrain is upstream of texture sampling and RPU composition, not missing ROM data.

The next accuracy target is the patent execution unit's exact 16-to-32-bit type conversion and multiply/shift/saturation sequence. Matrix elements deliberately retain a 32-bit sign/upper half while firmware changes their low word, so a plain signed-low-16 interpretation is not yet sufficient evidence. Once APV emits non-clipped polygons in the cold-boot fixture, the next structural task is the scanline YSU/RPU merge.e` plus the YSU/RPU scanline merge still need hardware-accurate lighting and ordering.

## Remaining implementation order

1. Match the remaining eye-plane and viewport clipping behavior against DBZ/DB2J command captures.
2. Cross-check the now patent-sized Q15.1 APV terrain and model scale in DB2J and later DBZ battle environments.
3. Extend hardware fixtures for the implemented `TD`, `Ccalc` and `TDBP` lighting chain, especially rounding boundaries and moving light vectors.
4. Replace whole-frame drawing passes with the patented scanline RPU: Y-sorted polygon/sprite streams, recycle queue, per-line depth merge, slicer, interpolator and two line buffers.
5. Finish remaining palette and edge-order cases after the scanline merger replaces the current compatibility passes.

Enemy visibility, Take-copter buildings, moving terrain and transparent layers are validation points of this shared hardware path, not separate game hacks.
