# E-IME development entry points

## Ownership

- `engine/include/`: parsing, composition, candidate selection and decoding.
- `tsf/`: Windows events, COM lifecycle and host edit sessions.
- `ui/`: candidate and mode-toast rendering; no host text writes.
- `sync/`: learning journals. Test data must remain isolated from user data.
- `tests/`, `testbench/`: development-only tests and host mocks.
- `tools/`: pinned third-party data generators. Follow THIRD_PARTY_NOTICES.md.

## Build and verification

Use PowerShell 7 on Windows. Shared entry points are in CMakePresets.json.

- MinGW: `cmake --preset mingw-dev`, `cmake --build --preset mingw-dev`, `ctest --preset mingw-dev`.
- MSVC: use the corresponding `msvc-dev` presets.
- Product boundary: configure/build `mingw-release` or `msvc-release`.
- Stage Runtime with `cmake --install build/<release-preset> --config Release --prefix out/runtime --component Runtime`.
- Check the staged allowlist with `pwsh -File scripts/verify-runtime.ps1 -Path out/runtime`.
- Runtime staging checks do not register the IME or change the active DLL.

## Invariants

- Preserve key-test/key-down agreement and ordered edit snapshots.
- Host text writes occur inside TSF edit sessions; preserve generation checks.
- Keep phonetics and literal characters in one ordered composition buffer.
- Production targets must not depend on tests, mocks or analysis executables.
- Install explicit Runtime files only; do not copy whole source/build/data trees.
- Never package custom words, journals, caches or local data-root pointers.
- Keep tool configuration and behavior fixtures in this repository; store generated diagnostics outside tracked sources.
- ETW workflow: build/test `mingw-diagnostic` or `msvc-diagnostic`, then run `pwsh -File scripts/diagnose.ps1 -Action Verify -BuildDirectory build/<diagnostic-preset>`.
- Verification records synthetic input in an isolated fixture and injects exactly one expected host error. Do not treat that deliberate failure as a production regression.
- Real host capture requires explicitly installing the diagnostic DLL and reopening the host; release DLLs emit no events.
- Reuse established analysis tools; see docs/DIAGNOSTICS.md. TTD/fuzz tooling is not implemented.

See docs/DEVELOPMENT.md for build boundaries and remaining work.
