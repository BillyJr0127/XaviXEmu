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
| `ttv_mx` | 8,388,608 | `e64bf1a1` | `137f97d7d857697a13e0c8984509994dc7bc5fc5` |
| `tom_jump` | 8,388,608 | `20bf5c17` | `bca7535baa6a54ad3ee0929bd3b74a22cb5139da` |
| `epo_sdb` | 4,194,304 | `a004a764` | `47a96822d4d7d6a0f6be5cd729c3747dbab65979` |
| `epo_ebox` | 4,194,304 | `e25ae4f5` | `7f7b613f0ab8f43f5cad0d13de538921e77cae9c` |
| `epo_es2j` | 4,194,304 | `840aecb1` | `ad52449ffc13af5f4c67b2c3cf438e7ecd80b9fb` |
| `epo_hamc` | 4,194,304 | `b1177813` | `ed01096ebb63b72267ad7e0b2115224bbab64011` |
| `tom_dpgm` | 4,194,304 | `1dc181b3` | `fa30069d17705f27e4ff45e7f6ccf06986e138f3` |
| `epo_mini` | 4,194,304 | `2adb01ee` | `987218b6799195ba15adf39885c1d177c381ec26` |
| `epo_bowl` | 2,097,152 | `d34f8d9e` | `ebe3792172dc43904b9226beb27f1da89d2388cc` |
| `tak_chq` | 4,194,304 | `ffd2eb95` | `a30884da5554483ebfd0009cf5dd1768be8a99cb` |
| `epo_hamd` assembled image | 8,388,608 | `427cb00f` | `c61d436d6b803717b8c84d2022499380f71cced8` |
| `tvpc_dor` | 4,194,304 | `6f2edbb2` | `98fa86f85e00aa40e7a585ff0bc930cb5ca88362` |
| `tvpc_ham` | 4,194,304 | `76e8c854` | `5998c03292a16107d0d7ae00f77677582680f323` |
| `tvpc_hk` | 4,194,304 | `87fc2f73` | `29a284b907abec175d4289d290490af17a2a963f` |
| `ban_naru` | 8,388,608 | `e3465ad2` | `13e3d2de5d5a084635cab158f3639a1ea73265dc` |
| `ban_bldj` | 8,388,608 | `aa865fe3` | `2f5f4809a07a2f5671f81aa22e379c11c43943a0` |
| `ban_db2j` | 8,388,608 | `7362ac0d` | `f1880470f0db56135d9bc88d7193d037ac49b996` |
| `ban_dbz` | 8,388,608 | `7e535ea2` | `6c746af763273bd9e47929c3ba857c7af563bf79` |
| `epo_dab2j` | 8,388,608 | `e3d12ee6` | `a2f930f4ffe778e02556b5e1a1836f88888e7c82` |
| `epo_dtcj` | 8,388,608 | `64c2aabb` | `14f02eb01f1c6e76202f7a70818c300ba23fd879` |
| `epo_pabj` | 8,388,608 | `ac46991c` | `06c2b493824085502e96a7c1e46e9e89433e7301` |
| `epo_ssk2` | 8,388,608 | `d5902e48` | `010bc2417814ded24a474d9165f6b9523af7d1ef` |
| `epo_sskj` | 8,388,608 | `3344b2fc` | `cda27bd1c7d6ccdb6da06cd837aa9cde5a58e5e4` |

The exact `ban_dbz` image above visibly identifies itself as `体験版` on its
title screen and enters random battles directly. It is therefore treated as a
store-demo image even though external machine lists use the retail product's
generic title. No distinct retail dump is present in the supported ROM set.

`epo_hamd` is stored as two verified chips in the ZIP. XaviXEmu places the
1,048,576-byte image with CRC32 `6c2d9d98` and SHA-1
`89a8e6d236ea3dadb882e3ecf12e41bd50222710` at offset `0x000000`, and the
2,097,152-byte image with CRC32 `e437c8d0` and SHA-1
`f57c54a73ed38826f4b98610a0aa1f15cf95614d` at offset `0x400000` in a
zero-filled 8 MiB address space. Member filenames are not trusted as identity.

Unknown images and modified dumps are rejected rather than guessed.
