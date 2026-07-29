# Releasing FriedasBirdview

The `project(... VERSION ...)` value in `CMakeLists.txt` is the single source
of truth for every release artifact: the Windows Setup filename/version, the
host-side upload script, and the release-tag validation in GitHub Actions.

For every user-visible code, packaging, or security change, raise at least the
minor version before committing (for example, `1.1.0` to `1.2.0`). Use a major
version only for a deliberately incompatible release. Documentation-only
changes do not require a release version bump.

Before publishing:

1. Update `CMakeLists.txt` and add a matching `CHANGELOG.md` section.
2. Build and smoke-test Linux/Flatpak and the Windows installer.
3. Commit the release changes, then create the exact matching signed tag:

   ```sh
   git tag -s v1.1.0 -m "FriedasBirdview 1.1.0"
   git push origin v1.1.0
   ```

GitHub Actions and `build-and-install-windows.sh` reject a release tag that
does not equal `v` followed by the CMake project version. Upload the verified
Windows Setup asset only after the tag workflow has created its GitHub Release.
