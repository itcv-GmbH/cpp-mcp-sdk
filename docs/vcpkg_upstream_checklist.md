# vcpkg Upstream Checklist

Use this checklist when moving from a local overlay port to an upstream vcpkg port submission.

## 1) Stable Versioning and Tags

- Publish a stable release tag that matches the port version metadata.
- Keep source archives reproducible (tagged source tarball/hash must remain stable).
- Update the port version using vcpkg conventions (`version`, `version-semver`, or `version-date`) and include `port-version` when patching without upstream version changes.

## 2) License and Copyright Mapping

- Ensure a canonical license file exists at repository root (`LICENSE` or equivalent).
- Set SPDX-compliant `license` metadata in `vcpkg.json`.
- Map upstream license text into `share/<port>/copyright` in the port.
- Confirm third-party bundled code (if any) has compatible licensing and attribution.

## 3) CI Evidence and Reproducibility

- Capture successful CI logs for representative triplets (at least one Linux/macOS/Windows combination where applicable).
- Verify clean configure/build/install for the port and a consumer using `find_package(mcp_sdk CONFIG REQUIRED)`.
- Store exact commands used for verification (including any `--overlay-ports` usage during local validation).
- Confirm no absolute developer-local paths leak into installed CMake config files.

## 4) Port Hygiene Before Submission

- Prefer fetching a tagged release/archive in `portfile.cmake` for upstream submission (not local `SOURCE_PATH`).
- Keep dependencies minimal and accurately scoped (host vs target dependencies).
- Run `vcpkg format-manifest` and vcpkg CI/lint checks before opening the upstream PR.
