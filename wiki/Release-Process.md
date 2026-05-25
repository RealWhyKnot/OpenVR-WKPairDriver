# Release process

Releases are produced by `.github/workflows/release.yml` on a `v*` tag push. The workflow:

1. Promotes the `## Unreleased` heading in `CHANGELOG.md` (and the wiki mirror) to `## [vTAG] -- DATE` via `.github/scripts/Update-Changelog.ps1`.
2. Configures + builds the umbrella exe and driver DLL with `-DWKOPENVR_RELEASE_BUILD=ON`. Modules carrying `modules/<slug>/disabled-in-release.flag` are skipped via the CMake helpers in `cmake/WKOpenVRModules.cmake` and their resource directories are wiped from the cached build tree before staging.
3. Runs every `*_tests.exe` under `build/artifacts/Release/`; any non-zero exit fails the release.
4. Stages a driver tree under `release/_stage_<version>/` with the loader-prefixed `driver_01wkopenvr.dll`, the umbrella exe, and `openvr_api.dll`. Resource directories for any module with `disabled-in-release.flag` are skipped.
5. Builds the umbrella zip (`WKOpenVR-<version>.zip`).
6. Builds the umbrella NSIS installer (`WKOpenVR-v<version>-Setup.exe`) plus one per-feature installer per active module (`WKOpenVR-<Name>-v<version>-Setup.exe`). Each per-feature installer drops the matching `enable_<feature>.flag` so the feature activates immediately after install.
7. Generates the release body via `.github/scripts/Generate-ReleaseNotes.ps1` from the promoted changelog plus a two-row file integrity table (umbrella zip + umbrella Setup.exe).
8. Publishes a single GitHub release on this repo with the umbrella zip, umbrella Setup.exe, and every per-feature Setup.exe attached.
9. Pushes the promoted CHANGELOG.md back to `main` via GraphQL `createCommitOnBranch` so the commit is server-side signed by GitHub's bot key.

## Changelog discipline

The `## Unreleased` section in `CHANGELOG.md` is auto-appended by `.github/workflows/changelog-append.yml` from conventional-commit subjects (`feat`, `fix`, `perf`, `refactor`, etc.) pushed to `main`. The release workflow renames `## Unreleased` to `## [v<tag>] -- <date>` at promotion time. The promoted file is the same one that lands in `main` after publish, so `git log -- CHANGELOG.md` shows the canonical history.

Hand-editing the release body is not part of the workflow. Extra prose for a tag goes in `release/<tag>/extra-details.md` if needed; the body generator picks that up.

## Commit-message hygiene

Two hooks enforce the version-stamp convention on `.githooks/`:

- `prepare-commit-msg` appends the current `version.txt` stamp to the subject if (and only if) the subject is clean. It rejects fresh commits whose subjects already carry a stamp -- the hook is the single source of truth; pre-stamps by humans or agents go stale on the next build.
- `commit-msg` rejects subjects with more than one stamp.

`.github/workflows/commit-msg-check.yml` mirrors the rule on the server so a push that bypassed the local hooks (a fresh clone where `build.ps1` hasn't activated `core.hooksPath` yet, `--no-verify`, an API commit) still surfaces as a failed check in the Actions tab.

## Test-tag rule

Don't push test tags to the live repo. The workflow publishes a public release per tag and pushes a server-signed changelog-promotion commit at the end; a "test" tag would leak all of that publicly. Validate workflow changes locally with `js-yaml` for parse, run the packaging step by extracting the PowerShell from the YAML and executing it against the existing build artifacts, or use a private fork for end-to-end runs.
