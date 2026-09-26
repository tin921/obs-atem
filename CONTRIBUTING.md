# Contributing

Contributions are welcome. Please read this before opening a pull request.

## What we want

- Bug fixes with a clear reproduction case
- OBS API compatibility updates (e.g. new dock registration API in OBS 30+)
- Improved error messages and connection diagnostics
- Testing with different ATEM Mini models or firmware versions

## What we don't want (right now)

- Cross-platform ports — BMDSwitcherAPI is COM/Windows-only by design
- Support for ATEM models beyond ATEM Mini (untested, and we lack hardware)
- GUI theme changes without a concrete use-case

## Getting started

1. Fork the repository
2. Build following the steps in [README.md](README.md)
3. Test against a real ATEM Mini (`atem-cli.exe` and `atem-harness.exe` help
   check the SDK layer and the panels without OBS)
4. Open a pull request describing what changed and why

## Code style

- C++17, MSVC-compatible
- Qt types preferred over raw Win32 except in COM interop code
- No comments unless the WHY is genuinely non-obvious
- One concern per source file — keep COM wrappers, UI panels, and the OBS entry point separate

## License

By contributing, you agree your contribution is licensed under the terms in
[LICENSE](LICENSE).
