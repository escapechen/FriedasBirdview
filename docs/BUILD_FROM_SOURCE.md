# Build FriedasBirdview from Source

## Before you start

Install a C++20 compiler, CMake, Qt 6 with Multimedia, WebEngine, and
WebChannel support, plus the KF6 Wallet and OpenSSL development packages.
Install the NSS `certutil` tool for custom-CA support in Qt WebEngine. KDE
Wallet must be enabled only if the Frigate server uses a username/password
login.

## Build and install for your user

```sh
./build-and-install.sh
```

This installs the executable, desktop entry, and icon below `~/.local`; no
administrator password is required. If `~/.local/bin` is not on your `PATH`,
launch FriedasBirdview from Plasma’s application menu after refreshing it or
logging in again.

## Manual build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
cmake --install build --prefix ~/.local
```

The `build/` directory is disposable and ignored by Git.

## Start automatically at sign-in

Enable **Start FriedasBirdview automatically when I sign in** in the app’s
Settings. It creates an XDG autostart desktop entry below
`~/.config/autostart` (or `XDG_CONFIG_HOME`) that launches the installed
binary directly. This works across Linux desktops that implement the
freedesktop autostart standard; no system service or distro-specific setup is
required.

## If the build fails

| Message | Fix |
| --- | --- |
| Qt6 WebEngine is missing | Install the Qt 6 WebEngine development package for your distribution. |
| Qt6 Multimedia is missing | Install the Qt 6 Multimedia development package for your distribution. |
| KF6Wallet is missing | Install the KF6/KWallet development package. |
| OpenSSL is missing | Install the OpenSSL development package. |
| `certutil` is unavailable | Install the NSS tools package; custom CAs require it for Qt WebEngine trust. |
| Password cannot be saved at runtime | Enable KDE Wallet and unlock or create a wallet, then apply settings again. |
| The app is not in the launcher | Refresh Plasma’s application menu or log out and back in after installing below `~/.local`. |
