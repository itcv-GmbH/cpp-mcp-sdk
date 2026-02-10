# Task ID: [task-038]
# Task Name: [CI Matrix (Linux/macOS/Windows)]

## Context
Add deterministic CI to build and run conformance tests across supported platforms.

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (Platform support; deterministic CI)

## Output / Definition of Done
* `.github/workflows/ci.yml` builds and runs tests on:
  - Linux x86_64
  - macOS arm64
  - Windows x86_64
* CI installs dependencies via vcpkg manifest mode and runs `ctest`
* CI pins the vcpkg baseline/commit (matches `vcpkg.json` `builtin-baseline`)

## Step-by-Step Instructions
1. Implement GitHub Actions workflow with matrix.
2. Install vcpkg and run `vcpkg install` (manifest mode) for each job.
3. Configure CMake using the vcpkg toolchain file.
4. Build Debug + Release (or at least one), run unit/conformance tests.
4. Optionally add sanitizers on Linux/macOS.

## Verification
* CI green on all platforms
