# Windows port handoff

This document is the starting point for a future native Windows build of
FriedasBirdview. It describes the intended architecture and the first setup;
it contains no production connection details or credentials.

## Status

- Linux/KDE and Flatpak are the current supported targets.
- The public `v1.0.1` release is a Linux Flatpak release.
- No Windows-specific source or installer exists yet.
- The CMake project currently reports version `1.0.0`; align that internal
  version with the next release tag before making a Windows release.

## What is already portable

The Qt Widgets UI, Frigate HTTP client, event filtering, JPEG feed, sound
notifications, Qt Multimedia, and Qt WebEngine MSE player should be shared.
Build the Windows application with the same CMake project rather than starting
a separate application.

## Required free software on Windows 11

Install these in this order:

1. **Visual Studio Community 2022** with the **Desktop development with C++**
   workload. Include MSVC v143 x64/x86, CMake tools, Ninja, and the Windows 11
   SDK.
2. The **Qt Online Installer**, open-source edition. Install a 64-bit
   `msvc2022_64` Qt 6 kit. Prefer the same Qt 6.10 series used by the Flatpak
   runtime and select Qt WebEngine.
3. **Git for Windows** for repository access.
4. **vcpkg**, then install the CMake-visible OpenSSL development package:

   ```powershell
   git clone https://github.com/microsoft/vcpkg C:\src\vcpkg
   C:\src\vcpkg\bootstrap-vcpkg.bat
   C:\src\vcpkg\vcpkg install openssl:x64-windows
   ```

Allow roughly 25 GB of free disk space. Qt WebEngine and its build artifacts
are large.

## First Windows build

Do this only after the platform dependency changes below have landed. Open a
Visual Studio Developer PowerShell and adapt `QT_ROOT` to the installed kit:

```powershell
$env:QT_ROOT = 'C:\Qt\6.10.0\msvc2022_64'
cmake -S . -B build-win -G Ninja -DCMAKE_BUILD_TYPE=Debug `
  -DCMAKE_PREFIX_PATH="$env:QT_ROOT" `
  -DCMAKE_TOOLCHAIN_FILE=C:\src\vcpkg\scripts\buildsystems\vcpkg.cmake
cmake --build build-win
```

The current project cannot configure unchanged on Windows, because Linux-only
dependencies are required unconditionally. That is the first porting task,
not a tool-installation failure.

## Required platform boundaries

Keep each of these behind a narrow interface with a Linux and Windows
implementation. Shared application code should depend only on the interface.

| Concern | Current Linux implementation | Windows target |
| --- | --- | --- |
| Frigate password | KWallet | Windows Credential Manager or DPAPI-backed credential store |
| Autostart | XDG desktop entry / Flatpak Background portal over DBus | Per-user Windows startup registration; no administrator rights |
| Custom WebEngine CA | Private NSS database managed with `certutil` | A dedicated, security-reviewed Windows trust strategy |
| Build dependencies | Qt DBus and KF6Wallet required by CMake | Do not require either on Windows |

### Credentials

Introduce a `CredentialStore` abstraction. The existing KWallet behavior
becomes the Linux implementation; Windows uses the built-in Credential Manager
or DPAPI. The server URL and user name may stay in `QSettings`, but the password
must never do so.

### Autostart

Keep the existing Linux/Flatpak behavior. On Windows, use a per-user startup
mechanism (normally the `HKCU` `Run` registry key) and quote the executable
path safely. Do not require elevation, create a scheduled task, or use a
machine-wide registry key.

### Custom certificate authorities

The present NSS `certutil` commands and private NSS database are Linux-specific;
Windows `certutil.exe` is not a compatible replacement. Preserve normal TLS
validation for both Qt Network and Qt WebEngine. First determine whether the
target Qt WebEngine version supports app-scoped trust on Windows. If Windows
requires a Current User root-store import for WebEngine, present that as an
explicit, clearly scoped user choice rather than doing it silently. Continue to
accept only valid CA certificates and remove only certificates that this app
added.

### CMake layout

Split platform sources in CMake. `Qt6::DBus` and `KF6::Wallet` must be found and
linked only for Linux. The common target should continue to require Qt Core,
Gui, Widgets, Network, Multimedia, WebChannel, WebEngineWidgets, and OpenSSL.
Do not make the Windows build depend on KDE Frameworks.

## Deployment and packaging

After a Release build succeeds, use the `windeployqt.exe` belonging to the same
Qt kit to collect Qt DLLs, QtWebEngineProcess, resources, locales, and the MSVC
runtime. Do not hand-copy DLLs from the build tree.

```powershell
& "$env:QT_ROOT\bin\windeployqt.exe" --release --compiler-runtime `
  .\build-win\friedasbirdview.exe
```

Add a Windows installer only after the deployed directory works on a clean VM.
An NSIS/CPack or Inno Setup installer is sufficient; keep it separate from the
Flatpak and Gentoo packaging.

## Verification checklist

- Configure and build Debug and Release with MSVC x64.
- Run on a clean Windows VM after `windeployqt` deployment.
- Verify password storage is absent from the registry/settings files.
- Verify autostart enables and disables only the current user's entry.
- Verify a normal public HTTPS endpoint works and invalid certificates remain
  rejected.
- Test custom-CA behavior with a disposable CA; do not use a home CA in source,
  screenshots, or automated logs.
- Confirm JPEG and MSE feeds, popup geometry, sound, cooldowns, and close/
  focus behavior match the Linux version.
- Rebuild and smoke-test Linux before merging.

