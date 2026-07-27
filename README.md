# FriedasBirdview

![Frieda walking through a twilight garden](docs/images/frieda-birdview-hero.png)

FriedasBirdview is a small KDE Plasma tray app compatible with
[Frigate NVR](https://github.com/blakeblackshear/frigate). When Frigate detects
something you care about—such as a person, pet, bird, or named animal—the app
opens a temporary camera feed above your work without taking keyboard focus.

> FriedasBirdview is an independent application compatible with Frigate. It is
> not affiliated with or endorsed by Frigate, Inc.

Built by [Marcel Kühn](AUTHORS.md) with OpenAI Codex (GPT-5.6 Terra, Extra High
reasoning).

The Frieda artwork in this repository was created from photographs supplied by
the project maintainer.

![FriedasBirdview activity popup showing a cat detection](resources/FriedasBirdview-Popup.png)

## What it does

- Watches recent Frigate events and review activity every two seconds.
- Can start itself at sign-in through the freedesktop XDG autostart standard,
  or the desktop Background portal when installed as Flatpak.
- Opens a movable, timed feed for selected classifications or any tracked
  object.
- Shows the detected label, confidence, camera, and countdown.
- Uses JPEG snapshots by default, with an authenticated low-latency go2rtc MSE
  live-stream option.
- Can play an opt-in, user-selected sound for a newly detected matching event.
- Offers independent, optional cooldowns for automatic popups and sound alerts.
- Stores an optional Frigate password only in KDE Wallet; settings and the
  WebEngine profile do not persist passwords or session cookies.
- Remembers the feed window’s size and position without taking keyboard focus.
  In a Plasma Wayland session it uses the available Xwayland compatibility
  backend, because standard Wayland does not permit an app to restore an
  absolute top-level position.
- Shows connection-lost/restored notifications and a red tray icon when
  Frigate cannot be reached.

## Requirements

- KDE Plasma on a current Linux distribution, under Wayland or X11.
- CMake, a C++20 compiler, Qt 6 (Core, Gui, Widgets, Network, Multimedia,
  WebChannel, and WebEngineWidgets), and the KF6 Wallet development package.
- A reachable [Frigate NVR](https://github.com/blakeblackshear/frigate) server.
- A certificate your Linux trust store accepts, or its issuing CA certificate
  for the in-app custom-CA picker. FriedasBirdview deliberately keeps normal
  TLS validation enabled.

On distributions that split development packages, install the Qt WebEngine and
KWallet development packages, OpenSSL development files, and the NSS
`certutil` tool in addition to the base Qt 6 and KF6 packages.

## Build and install

From this repository:

```sh
./build-and-install.sh
```

The script builds a release binary and installs it below `~/.local`. Log out
and back in, or refresh your Plasma application menu, if it does not appear
immediately. You can also run the binary directly from `build/friedasbirdview`.
Pass an alternative prefix as the script’s first argument if needed.

To choose another install location, use CMake directly:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
cmake --install build --prefix /your/chosen/prefix
```

## Packages

- **Flatpak bundle:** download
  [`friedasbirdview-x86_64.flatpak`](https://github.com/escapechen/FriedasBirdview/releases/latest/download/friedasbirdview-x86_64.flatpak)
  from the latest GitHub Release, then install it with
  `flatpak install --user ./friedasbirdview-x86_64.flatpak`.
- [Flatpak build and Flathub submission notes](docs/FLATPAK.md) — the
  cross-distribution package for KDE, GNOME, and other Linux desktops.
- [Gentoo ebuild and local-overlay instructions](docs/GENTOO.md).

## First-time setup

![FriedasBirdview Settings](resources/FriedasBirdview-Settings.png)

1. Start **FriedasBirdview** and open its tray icon’s **Settings**.
2. Optionally enable **Start FriedasBirdview automatically when I sign in**.
   Native installs use your desktop’s XDG autostart location. Flatpak installs
   ask the desktop’s Background portal for approval instead.
3. Enter the full Frigate base URL, including `https://` and any non-standard
   port, such as `https://frigate.example.net:8971`, then choose **Apply**.
   If the scheme is omitted, FriedasBirdview uses `https://`; an explicit
   `http://` address remains available only for a deliberately insecure local
   deployment.
4. If Frigate has login enabled, enter the username and password. The password
   is saved in KDE Wallet, not in the application settings. Leave Password
   empty when applying unrelated settings to retain the saved password.
5. If Frigate uses a private CA, add its public root/issuing CA certificate in
   **Custom certificate authorities**, then restart FriedasBirdview before
   using live video. This adds only that CA as a trust anchor; certificate
   host-name and expiry checks remain enabled.
6. Set **Keep feed open** and select **JPEG snapshots** or **Live stream**.
7. Optionally enable **Sound alerts**, then choose a sound and volume. Use
   **Preview** to test the selected alert.
8. Optionally enable a **Popup cooldown** or **Sound cooldown** and choose an
   interval from 1 second to 1 hour. Manual **Show Feed** and sound **Preview**
   remain available during a cooldown.
9. Select **Selected classifications** and tick the labels/sub-labels that
   should open a feed, or choose **Any tracked object**.
10. Use **Refresh from Frigate** to retrieve known labels and sub-labels. You
   can always add a custom name, for example `Frieda`.

Fresh installs use the intentionally unreachable `https://frigate.invalid`
placeholder. A connection failure leaves the app running with the disconnected
tray state; it never terminates the app.

## Using the feed

- Click the image or video to dismiss it early.
- The lower-right countdown shows the automatic dismissal time.
- Drag the hand control to move the window.
- Use the diagonal-arrow button to enlarge or restore the feed.
- The requested position and size are preserved. In a Plasma Wayland session,
  FriedasBirdview uses Xwayland automatically when it is available so the
  saved position can be restored without manual KWin rules.

For **Live stream**, the selected camera needs a working Frigate/go2rtc MSE
restream and a codec Chromium can play. FriedasBirdview uses the Frigate
configuration mapping `cameras.<camera>.live.streams`; a camera name is not
assumed to be the go2rtc stream name. **JPEG snapshots** remain the compatible
fallback if live playback is unavailable.

## Important Frigate distinction

Birdseye’s **continuous**, **motion**, and **objects** modes decide what
Frigate draws in its Birdseye mosaic. They do not create a FriedasBirdview
popup by themselves. FriedasBirdview reacts to new Frigate event and review
records and filters them by object classification.

## Troubleshooting

| Problem | What to check |
| --- | --- |
| Tray icon is red | Confirm the URL, Frigate availability, TLS trust, and login details. |
| No popup | Confirm monitoring is running. Temporarily choose **Any tracked object**, then inspect the tray menu’s last activity. |
| A new event did not reopen the feed or play a sound | Check whether the respective cooldown is enabled and has not yet elapsed. |
| A name never triggers | Add the exact event label or sub-label reported by Frigate. A Birdseye image without a new event does not count. |
| Password cannot be saved | Enable and unlock KDE Wallet, then apply the settings again. |
| The app does not start after sign-in | Confirm the **Startup** switch is enabled. Native installs require XDG autostart; Flatpak installs require a desktop Background portal and its approval. Re-enable native autostart after moving the executable. |
| No sound is heard | Enable **Sound alerts**, use **Preview**, and check the desktop output device and volume. |
| Private CA works for JPEG but not live video | Add the issuing CA in **Custom certificate authorities**, then fully restart FriedasBirdview. Native packages require the NSS `certutil` tool; the Flatpak includes it. |
| Live stream is black or frozen | Confirm the stream plays in Frigate and a compatible go2rtc codec is available; choose JPEG snapshots as the immediate fallback. |
| Feed is on the wrong screen | Drag it to the intended display once. On Wayland placement is ultimately controlled by KWin. |

## Project notes

- [Build details](docs/BUILD_FROM_SOURCE.md)
- [Flatpak package](docs/FLATPAK.md)
- [Gentoo ebuild and local-overlay instructions](docs/GENTOO.md)
- [Changelog](CHANGELOG.md)
- [Third-party notices](THIRD_PARTY_NOTICES.md)
- [Contributors](AUTHORS.md)

FriedasBirdview is licensed under the [MIT License](LICENSE). Frigate-derived
MSE handling and the OpenSSL X.509 API use are covered by the attribution in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
