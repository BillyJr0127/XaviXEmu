# Contributing to XaviXEmu

Thank you for helping document and emulate XaviX hardware. Contributions must
be technically reproducible and legally redistributable.

## Before contributing

- Read `README.md`, `docs/compatibility.md`, and the relevant research note.
- Search existing issues once the public repository is available.
- Keep changes focused. Avoid game-specific check skipping unless it is clearly
  marked as a temporary diagnostic experiment.

## Build and test

Use the supported MinGW-w64 configuration:

```powershell
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

New emulation behaviour should include a ROM-independent regression test when
practical. ROM-dependent probes may be added as developer tools but must not
be registered in CI or contain captured game data.

## Evidence expected for emulation changes

Describe:

1. the observed symptom;
2. the PC, memory address, or I/O register involved;
3. the hardware behaviour being modelled;
4. the experiment used to distinguish competing explanations;
5. the result and remaining uncertainty.

Prefer the smallest change supported by evidence. Do not add permanent hacks
that merely skip a game check.

## Prohibited submissions

Never commit or attach:

- ROM, BIOS, firmware, or extracted game data;
- EEPROM images, save states, RAM dumps, or trace dumps containing game data;
- official logos, artwork, character images, box art, or gameplay screenshots;
- proprietary SDK files, leaked documentation, credentials, or signing keys;
- compiled EXE, DLL, PDB, or build directories.

ROM identification metadata such as shortname, size, CRC32, and SHA-1 is
acceptable.

## Licensing and attribution

By contributing, you agree that your contribution is available under the
repository's BSD 3-Clause License and that you have the right to submit it.
Do not remove or replace upstream copyright notices. Code adapted from another
project must identify its source file, revision, copyright holder, and license.

AI-assisted contributions are permitted, but the contributor remains
responsible for review, correctness, provenance, testing, and disclosure when
appropriate. Do not list an AI service as a copyright holder solely because it
was used as a tool.

## Style

- Preserve the existing C style and tab indentation.
- Use fixed-width integer types for emulated state.
- Keep host presentation state separate from portable emulation state.
- Avoid unrelated cleanup in hardware changes.
