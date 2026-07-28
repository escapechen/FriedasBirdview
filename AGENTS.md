# FriedasBirdview — agent handoff

## Scope and ownership

- FriedasBirdview is a Qt/C++ Frigate companion for KDE/Linux, with a planned
  native Windows port.
- Marcel Kühn owns commits, release signing, publishing, and repository
  visibility. Do not commit, tag, sign, push, publish, or change GitHub
  settings unless he explicitly asks.
- Start by reading `README.md`, then the task-relevant document under `docs/`.
  For Windows work, always read `docs/WINDOWS_PORT.md` before editing.

## Security and privacy

- Never log, commit, display, or document real Frigate URLs, credentials,
  cookies, private certificate material, camera names, or home-network paths.
- Passwords must remain in an OS credential store; never put them in
  `QSettings`, source files, build files, or logs.
- Preserve normal TLS validation. Do not add `ignoreSslErrors`, broad
  certificate bypasses, or WebEngine flags that weaken certificate checks.
- Custom CAs must be validated as CA certificates and their scope must be clear
  to the user. Do not silently add trust to a system-wide store.

## Architecture

- CMake is the only build system. The shared Qt UI, Frigate client, stream
  player, and settings behavior should remain cross-platform.
- Keep platform behavior behind small interfaces and platform-specific source
  files; do not scatter `#ifdef` blocks through shared logic.
- The current Linux implementation uses KWallet, DBus/desktop entries for
  autostart, and an NSS database for WebEngine CAs. These need dedicated
  Windows replacements; see `docs/WINDOWS_PORT.md`.
- The go2rtc MSE stream routing must use Frigate's configured
  `cameras.<camera>.live.streams` mapping. JPEG snapshots remain the fallback.

## Working practices

- Run `git status --short` before editing and preserve unrelated changes.
- Use `apply_patch` for source edits. Run `git diff --check` after edits.
- Build and test the changed target in proportion to risk. A Windows change
  must not regress the Linux CMake build.
- Keep packaging platform-specific. Windows deployment uses Qt's
  `windeployqt`; do not copy build-tree DLLs manually.

