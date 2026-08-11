# Supported ROM metadata

XaviXEmu does not distribute ROM data. The following metadata is used only to
identify exact, user-supplied images.

| Shortname | Size (bytes) | CRC32 | SHA-1 |
| --- | ---: | --- | --- |
| `drgqst` | 8,388,608 | `3d24413f` | `1677e81cedcf349de7bf091a232dc82c6424efba` |
| `ban_onep` | 8,388,608 | `c5cb5a5f` | `db85f6cc48d77c5a4967b9b8e2999167e3dfc8c8` |
| `ban_omt` | 4,194,304 | `1c1dc6fb` | `d0cf1345b765d66ca9a0870ee6d0e3ccd84a8c0b` |
| `ttv_lotr` | 8,388,608 | `a034ecd5` | `264a9d4327af0a075841ad6129db67d82cf741f1` |
| `ttv_sw` | 8,388,608 | `51cae5fd` | `1ed8d556f31b4182259ca8c766d60c824d8d9744` |
| `ttv_swj` | 8,388,608 | `a5c22ed0` | `406f0bccb01cd4a26fe4a5675d7ebecc78c58147` |
| `epo_hamd` assembled image | 8,388,608 | `427cb00f` | `c61d436d6b803717b8c84d2022499380f71cced8` |
| `tvpc_dor` | 4,194,304 | `6f2edbb2` | `98fa86f85e00aa40e7a585ff0bc930cb5ca88362` |
| `ban_naru` | 8,388,608 | `e3465ad2` | `13e3d2de5d5a084635cab158f3639a1ea73265dc` |

`epo_hamd` is stored as two verified chips in the ZIP. XaviXEmu places the
1,048,576-byte image with CRC32 `6c2d9d98` and SHA-1
`89a8e6d236ea3dadb882e3ecf12e41bd50222710` at offset `0x000000`, and the
2,097,152-byte image with CRC32 `e437c8d0` and SHA-1
`f57c54a73ed38826f4b98610a0aa1f15cf95614d` at offset `0x400000` in a
zero-filled 8 MiB address space. Member filenames are not trusted as identity.

Unknown images and modified dumps are rejected rather than guessed.
