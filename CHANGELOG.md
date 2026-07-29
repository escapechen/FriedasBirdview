# Changelog

All notable user-facing changes are recorded here. This project follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and uses semantic
versioning when releases are tagged.

## [Unreleased]

## [1.8.0]

### Changed

- Windows live playback now uses the Windows 11 Edge WebView2 media stack,
  restoring H.264 MSE playback without distributing a proprietary codec build
  of Qt WebEngine.

### Fixed

- Fixed WebView2 live-video sizing on high-DPI Windows displays.

## [1.7.0]

### Fixed

- Live playback now detects a Qt WebEngine build with audio-only MSE support
  and switches directly to JPEG snapshots instead of repeatedly showing a
  black video panel.

## [1.6.0]

### Fixed

- Windows live playback now uses Chromium software rendering by default, which
  avoids black MSE video on virtual machines and affected graphics drivers.

## [1.5.0]

### Fixed

- Hardened live go2rtc MSE playback: keeps a bounded near-live buffer, retries
  recoverable connection and media errors, and automatically switches to JPEG
  snapshots when live playback is unavailable, stalls, or fails repeatedly.

## [1.4.0]

### Changed

- The Windows release handoff now fetches its requested tag and creates or
  safely resets a dedicated sibling worktree on the VM, leaving the ordinary
  development checkout untouched.

## [1.3.0]

### Fixed

- The Windows release handoff now passes named parameters correctly to the
  packaging script, allowing its tagged VM build to complete.

## [1.2.0]

### Added

- A shared KDE/Windows About dialog with app icon, exact build version, GitHub
  repository, changelog link, credits, and licence information.

## [1.1.0]

### Added

- Native Windows 11 build support with Windows Credential Manager password
  storage, per-user startup registration, and app-scoped custom-CA trust.
- An Inno Setup installer that includes the Qt deployment, Microsoft C++
  runtime, vcpkg OpenSSL runtime, Start-menu shortcut, and uninstaller.
- A host-side release handoff that builds on the configured Windows VM,
  verifies the downloaded installer checksum, and can upload it to GitHub.

### Security

- Windows builds enable MSVC `/sdl` and Control Flow Guard, plus ASLR, DEP,
  and high-entropy address-space linker mitigations.
- Release actions are pinned to immutable revisions, and project screenshots
  contain no deployment-specific connection or camera information.

### Fixed

- Windows installers now include `libcrypto` and `libssl` from vcpkg instead
  of failing at startup when the OpenSSL runtime is unavailable.

## [1.0.1]

### Added

- Optional XDG-standard automatic start at desktop sign-in, configurable from
  Settings.
- Optional sound alerts with three built-in tones, configurable volume, and a
  preview control. Alerts play only for newly detected matching activity and
  are rate-limited to one per second.
- Optional independent popup and sound cooldowns, configurable from 1 second
  to 1 hour.
- Flatpak packaging foundation using the KDE runtime and Qt WebEngine base app,
  including sandbox-specific application metadata and permissions.
- Flatpak automatic-start support through the desktop Background portal, with
  the desktop’s explicit approval.
- Automated GitHub Release bundles for x86_64 Flatpak installations, with a
  published SHA-256 checksum.

### Security

- Defaults a server address without an explicit scheme to HTTPS rather than
  clear-text HTTP.

### Fixed

- Keeps the Plasma tray menu's D-Bus action tree stable while its live status
  updates, preventing transient invalid-menu queries from the system tray.
- Avoids Qt's unused host-portal registration on normal desktop installs;
  portal support remains enabled in Flatpak and Snap sandboxes.
- Makes the Flatpak CA picker use the document portal and explains its
  Downloads fallback for protected host locations.
- Compacts the Settings dialog so its lists and groups consume less vertical
  space.

## [1.0.0]

### Added

- KDE Plasma system-tray monitoring for Frigate events and review activity.
- A non-activating floating feed with event reason, camera, countdown,
  remembered geometry, drag control, and enlarge/restore control.
- Selectable classifications, including custom labels and Frigate sub-labels.
- JPEG snapshots and authenticated go2rtc MSE live-stream playback.
- Optional Frigate login with passwords held only in KDE Wallet.
- Connection-lost/restored notifications and a red tray icon for unavailable
  servers.
- CMake build, per-user installation script, desktop entry, and source-build
  documentation.
- Gentoo ebuild with local-overlay and release-manifest instructions.

### Security

- Keeps normal TLS validation and uses an ephemeral WebEngine profile.
- Does not write passwords, session cookies, or configured server addresses to
  logs or project files.

[1.0.0]: https://github.com/escapechen/FriedasBirdview/releases/tag/v1.0.0
[1.0.1]: https://github.com/escapechen/FriedasBirdview/releases/tag/v1.0.1
[1.1.0]: https://github.com/escapechen/FriedasBirdview/releases/tag/v1.1.0
[1.2.0]: https://github.com/escapechen/FriedasBirdview/releases/tag/v1.2.0
[1.3.0]: https://github.com/escapechen/FriedasBirdview/releases/tag/v1.3.0
[1.4.0]: https://github.com/escapechen/FriedasBirdview/releases/tag/v1.4.0
[1.5.0]: https://github.com/escapechen/FriedasBirdview/releases/tag/v1.5.0
[1.6.0]: https://github.com/escapechen/FriedasBirdview/releases/tag/v1.6.0
[1.7.0]: https://github.com/escapechen/FriedasBirdview/releases/tag/v1.7.0
[1.8.0]: https://github.com/escapechen/FriedasBirdview/releases/tag/v1.8.0
