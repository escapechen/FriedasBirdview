# Windows build, installer, and release

FriedasBirdview has native Linux/KDE Plasma and Windows 11 x64 builds. This
document describes the Windows build, installer, and release process; it
contains no production connection details or credentials.

## Status

- Linux/KDE is available as a native build and Flatpak bundle.
- Windows 11 x64 is available as a native CMake/MSVC build and an Inno Setup
  installer with its runtime dependencies included.
- The CMake project currently reports version `1.10.0`; release it as tag
  `v1.10.0` after the Windows installer smoke test passes.

## What is already portable

The Qt Widgets UI, Frigate HTTP client, event filtering, JPEG feed, sound
notifications, and Qt Multimedia are shared. Windows uses a native Edge
WebView2 MSE view. Linux uses native go2rtc MSE with Qt Multimedia's system
FFmpeg backend by default; progressive MP4 and Qt WebEngine MSE remain
user-selectable compatibility paths. Both keep bounded buffers and
automatically fall back to JPEG snapshots when playback cannot recover.

### Windows live-video rendering

Windows live video uses Edge WebView2, which accesses the Windows-maintained
media stack instead of asking FriedasBirdview to distribute H.264/H.265 codec
libraries. The Evergreen WebView2 runtime is included with Windows 11 and
receives security updates independently of the app. A missing or incompatible
runtime leaves JPEG snapshots available with a clear error.

## Required free software on Windows 11

Install these in this order:

1. **Visual Studio Community 2022** with the **Desktop development with C++**
   workload. Include MSVC v143 x64/x86, CMake tools, Ninja, and the Windows 11
   SDK.
2. The **Qt Online Installer**, open-source edition. Install a 64-bit
   `msvc2022_64` Qt 6.5-or-newer kit. Qt WebEngine is not needed for the
   Windows build. The release VM is tested with Qt 6.11.1.
3. **Git for Windows** for repository access.
4. **vcpkg**, then install the CMake-visible OpenSSL development package:

   ```powershell
   git clone https://github.com/microsoft/vcpkg C:\src\vcpkg
   C:\src\vcpkg\bootstrap-vcpkg.bat
   C:\src\vcpkg\vcpkg install openssl:x64-windows
   ```

Allow roughly 10 GB of free disk space for Qt, Visual Studio, vcpkg, and build
artifacts.

## First Windows build

Open a Visual Studio Developer PowerShell and adapt `QT_ROOT` to the installed
kit. The helper downloads only the pinned C++ WebView2 SDK headers/static
loader; it does not download or package a browser runtime:

```powershell
$env:QT_ROOT = 'C:\Qt\6.11.1\msvc2022_64'
.\packaging\windows\ensure-webview2-sdk.ps1
cmake -S . -B build-win -G Ninja -DCMAKE_BUILD_TYPE=Debug `
  -DQt6_DIR="$env:QT_ROOT\lib\cmake\Qt6" `
  -DCMAKE_TOOLCHAIN_FILE=C:\src\vcpkg\scripts\buildsystems\vcpkg.cmake `
  -DFRIEDASBIRDVIEW_WEBVIEW2_SDK_ROOT="$env:LOCALAPPDATA\FriedasBirdview\build-tools\webview2-sdk-1.0.4022.49"
cmake --build build-win
```

The project selects its credential, autostart, live-video, and CA-trust paths
by platform. `KF6Wallet`, Qt DBus, and Qt WebEngine are Linux-only dependencies.

## Required platform boundaries

Keep each of these behind a narrow interface with a Linux and Windows
implementation. Shared application code should depend only on the interface.

| Concern | Current Linux implementation | Windows target |
| --- | --- | --- |
| Frigate password | KWallet | Windows Credential Manager |
| Autostart | XDG desktop entry / Flatpak Background portal over DBus | Per-user `HKCU\...\Run` value; no administrator rights |
| Live-video CA trust | Private NSS database managed with `certutil` | Windows Current User certificate store for WebView2 |
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
Windows `certutil.exe` is not a replacement. Qt Network uses the app-selected
custom CAs for Frigate API and JPEG requests. Edge WebView2 deliberately uses
the Windows Current User certificate store for MSE video, so import Frigate’s
issuing CA there when needed. FriedasBirdview does not add a root CA to the
system store or bypass host-name, expiry, or certificate-chain validation.

### CMake layout

Platform sources are selected in CMake. `Qt6::DBus`, `KF6::Wallet`, and Qt
WebEngine are Linux-only. Windows links the pinned Microsoft WebView2 SDK's
static loader plus the native Credential Manager library. The first package
build downloads that SDK from NuGet to the current user's build-tools cache,
verifies its pinned SHA-256, and does not include it in the installer.

## Deployment and end-user packaging

After a Release build succeeds, use the `windeployqt.exe` belonging to the same
Qt kit from a Visual Studio developer shell. It collects Qt DLLs and
`vc_redist.x64.exe`; WebView2 comes from Windows rather than the deployment
directory. Do not hand-copy DLLs from the build tree.

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
$env:QT_ROOT = 'C:\Qt\6.11.1\msvc2022_64'
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
# Edit only the ignored local config. Its Windows repository path names the
# ordinary source checkout; it may have local changes. Test a pushed commit
# before creating a tag or GitHub Release. The ref is resolved remotely after
# fetch, then built in a clean sibling candidate worktree.
./build-and-install-windows.sh --candidate origin/main
# Install and smoke-test the local candidate installer before tagging.
# The command prints its exact commit and stores it below:
# dist/windows/candidate-<12-character-commit>/

# After the tested commit is signed and tagged, build the immutable release.
./build-and-install-windows.sh v1.2.3
# After the GitHub Release exists, this reuses the locally verified assets; it
# does not rebuild the Windows VM package.
./build-and-install-windows.sh --publish v1.2.3
```

Candidate mode never creates or uploads a GitHub Release asset, and it refuses
`--publish`. Its clean VM worktree is named
`FriedasBirdview-candidate-<12-character-commit>`. The normal release tag must
exactly match the CMake version (`v1.2.3` for `1.2.3`); release mode creates or
resets only `FriedasBirdview-release-v1.2.3`. Neither mode changes the configured
source checkout. CTest failures, zero CTest tests (unless explicitly allowed
in the ignored config), hash mismatches, or a missing GitHub Release stop the
relevant step. Candidate assets live below ignored
`dist/windows/candidate-<12-character-commit>/`; release assets live below
`dist/windows/<tag>/`.

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
