# Windows port handoff

This document is the starting point for a future native Windows build of
FriedasBirdview. It describes the intended architecture and the first setup;
it contains no production connection details or credentials.

## Status

- Linux/KDE and Flatpak are the current supported release targets.
- The public `v1.0.1` release is a Linux Flatpak release.
- Windows has a native CMake/MSVC build path. It is not packaged or released yet.
- The CMake project currently reports version `1.2.0`; release it only as tag
  `v1.2.0` after the Windows installer smoke test passes.

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

Open a Visual Studio Developer PowerShell and adapt `QT_ROOT` to the installed
kit. Qt 6.10 or newer is required on Windows because WebEngine uses its
app-scoped additional-CA API:

```powershell
$env:QT_ROOT = 'C:\Qt\6.10.0\msvc2022_64'
cmake -S . -B build-win -G Ninja -DCMAKE_BUILD_TYPE=Debug `
  -DQt6_DIR="$env:QT_ROOT\lib\cmake\Qt6" `
  -DCMAKE_TOOLCHAIN_FILE=C:\src\vcpkg\scripts\buildsystems\vcpkg.cmake
cmake --build build-win
```

The project selects its credential, autostart, and WebEngine-CA implementations
by platform. `KF6Wallet` and Qt DBus are Linux-only build dependencies.

## Required platform boundaries

Keep each of these behind a narrow interface with a Linux and Windows
implementation. Shared application code should depend only on the interface.

| Concern | Current Linux implementation | Windows target |
| --- | --- | --- |
| Frigate password | KWallet | Windows Credential Manager |
| Autostart | XDG desktop entry / Flatpak Background portal over DBus | Per-user `HKCU\...\Run` value; no administrator rights |
| Custom WebEngine CA | Private NSS database managed with `certutil` | Qt 6.10+ app-scoped WebEngine profile trust |
| Build dependencies | Qt DBus and KF6Wallet | Neither is required on Windows |

### Credentials

`CredentialStore` keeps the existing KWallet behavior on Linux and uses a
Windows Credential Manager generic credential on Windows. The server URL and
user name may stay in `QSettings`, but the password must never do so.

### Autostart

Keep the existing Linux/Flatpak behavior. On Windows the app stores one safely
quoted executable path in the current user's `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`
key. Do not require elevation, create a scheduled task, or use a machine-wide
registry key.

### Custom certificate authorities

The NSS `certutil` commands and private NSS database remain Linux-specific;
Windows `certutil.exe` is not a replacement. With Qt 6.10 or newer, the app
passes validated custom CA certificates to the dedicated WebEngine profile as
additional trust anchors. It does not alter either Windows certificate store.
Qt Network receives the same CAs through its request TLS configuration. Normal
hostname and expiry validation remain enabled.

### CMake layout

Platform sources are selected in CMake. `Qt6::DBus` and `KF6::Wallet` are found
and linked only for Linux. The common target requires Qt Core, Gui, Widgets,
Network, Multimedia, WebChannel, WebEngineCore, WebEngineWidgets, and OpenSSL.
Windows links only the native Credential Manager library in addition.

## Deployment and end-user packaging

After a Release build succeeds, use the `windeployqt.exe` belonging to the same
Qt kit from a Visual Studio developer shell. It collects Qt DLLs,
QtWebEngineProcess, resources, locales, and `vc_redist.x64.exe`. Do not
hand-copy DLLs from the build tree.

```powershell
& "$env:QT_ROOT\bin\windeployqt.exe" --release --compiler-runtime `
  .\build-win\friedasbirdview.exe
```

`packaging/windows/package.ps1` produces the release deployment directory, then
creates an Inno Setup `Setup.exe` when Inno Setup 6 is installed. It packages
all `windeployqt` output, installs the Microsoft C++ Redistributable, adds a
Start-menu shortcut, and provides an uninstaller. It deliberately preserves
per-user settings and Credential Manager passwords on uninstall. Qt deployment
does not discover vcpkg runtime dependencies, so the script also copies the
matching vcpkg OpenSSL `libcrypto` and `libssl` DLLs.

Install Inno Setup 6 on the build machine once, for example with:

```powershell
winget install JRSoftware.InnoSetup
```

Then, from the same Visual Studio Developer PowerShell used to build:

```powershell
$env:QT_ROOT = 'C:\Qt\6.10.0\msvc2022_64'
.\packaging\windows\package.ps1
```

The installer needs one UAC elevation to install the recommended centrally
serviced Microsoft C++ runtime. End users do not need Qt, Visual Studio, vcpkg,
or Git. Use `-SkipInstaller` to create only the deployed directory; if Inno
Setup is absent, the script creates a portable ZIP as a fallback.

### Development signing

For local Windows development only, create a non-exportable test root and a
code-signing certificate issued by it. The command trusts the public root for
the current Windows user only:

```powershell
.\packaging\windows\new-dev-signing-certificate.ps1 -TrustForCurrentUser
```

If a Windows root-store policy blocks the non-interactive import, rerun with
`-ExportPublicCertificatePath C:\Temp\FriedasBirdview-development-root.cer`,
then import that file in `certmgr.msc` under **Certificates - Current User** >
**Trusted Root Certification Authorities**. Accept the warning only on a
disposable development machine.

Pass the printed thumbprint to the package command. This signs the application
executable before Inno Setup runs, then signs and verifies the final installer:

```powershell
.\packaging\windows\package.ps1 -SigningCertificateThumbprint <thumbprint>
```

The private keys never leave `Cert:\CurrentUser\My`; only export the public
root `.cer` file when a separate development VM needs to trust this test
signature. This trust is local to that user and must never be used for public
releases. Production releases need a publicly trusted signing provider and an
RFC-3161 timestamp.

### Host-side release handoff

`build-and-install-windows.sh` runs the package build and CTest checks over SSH
on a configured Windows VM, downloads the Setup installer and a SHA-256
manifest, verifies the downloaded hash locally, and can attach both assets to
an existing GitHub Release. It does not create tags, commits, or releases.

```sh
cp build-and-install-windows.conf.example build-and-install-windows.conf
chmod 600 build-and-install-windows.conf
# Edit only the ignored local config, then ensure the Windows checkout is clean
# and checked out at the same release tag.
./build-and-install-windows.sh v1.2.3
./build-and-install-windows.sh --publish v1.2.3
```

The tag must exactly match the CMake version (`v1.2.3` for `1.2.3`). The script
fails if the VM checkout is dirty, not at that tag, CTest fails, no CTest tests
are registered (unless explicitly allowed in the ignored config), the hashes do
not match, or the GitHub Release does not already exist. Local assets are kept
under ignored `dist/windows/<tag>/`.

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
