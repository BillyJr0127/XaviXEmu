# Security policy

## Supported versions

Until the first public release, only the current development snapshot is
maintained. After release, the latest `0.1.x` build will receive security
fixes; older alpha builds may be asked to upgrade before investigation.

## Reporting a vulnerability

Once the GitHub repository is public, use GitHub private vulnerability
reporting if it is enabled. If private reporting is unavailable, open a minimal
issue requesting a private contact channel without including exploit details,
ROM data, personal information, or secrets.

Include the affected version, Windows version, reproduction conditions, and
the security impact. Do not attach a ROM, save state, EEPROM image, memory dump,
or copyrighted game capture.

## Scope

Security reports may include malformed ZIP handling, path handling,
persistence corruption, memory-safety faults, or unsafe external URL/file
behaviour. Compatibility defects without a security impact belong in the
normal issue tracker.
