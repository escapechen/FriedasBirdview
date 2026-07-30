# Gentoo package

FriedasBirdview is packaged as `net-misc/friedasbirdview` for a local Gentoo
overlay. The ebuild is maintained in this source tree at
`packaging/gentoo/net-misc/friedasbirdview`.

## Release input

The ebuild fetches GitHub's automatic source archive for a version tag, for
example:

```text
https://github.com/escapechen/FriedasBirdview/archive/refs/tags/v1.11.0.tar.gz
```

Therefore a manually created `.tar.gz` or a GitHub Release is **not** required
for source installs. A pushed, immutable `v1.11.0` tag is required. The
distfile checksum is recorded in Portage's `Manifest` after the tag is
available.

For a new release, update the project version and changelog, then create and
push a signed tag:

```sh
git tag -s v1.11.0 -m "FriedasBirdview 1.11.0"
git push origin v1.11.0
```

A GitHub Release is optional at this stage. It becomes useful later for
separately signed binary artifacts such as an AppImage; it is not the source
used by this ebuild.

## Install from a local overlay

These commands create a small, local-only overlay. They assume the standard
Gentoo repository is already configured as `gentoo`.

Create the repository root, then copy the packaged overlay layout into it:

```sh
sudo mkdir -p /var/db/repos/friedasbirdview
sudo cp -a packaging/gentoo/. /var/db/repos/friedasbirdview/
```

Create `/etc/portage/repos.conf/friedasbirdview.conf` with `sudoedit`:

```ini
[friedasbirdview]
location = /var/db/repos/friedasbirdview
masters = gentoo
auto-sync = no
```

After `v1.11.0` exists on GitHub, generate the checksum manifest. This downloads
the source archive and records its hashes:

```sh
sudo ebuild /var/db/repos/friedasbirdview/net-misc/friedasbirdview/friedasbirdview-1.11.0.ebuild manifest
```

The first ebuild is keyworded `~amd64`. Accept that keyword locally:

```sh
sudoedit /etc/portage/package.accept_keywords/friedasbirdview
```

Add this line to the file:

```text
net-misc/friedasbirdview ~amd64
```

Then install it:

```sh
emerge --ask net-misc/friedasbirdview
```

Portage installs the executable, desktop entry, and hicolor icons system-wide.
The ebuild refreshes desktop and icon caches after install or removal.

## Notes

- The application needs `dev-qt/qtbase:6` with `X`, `gui`, `network`, and
  `widgets` enabled. It uses Xwayland under Plasma Wayland to preserve the
  feed window's position without manual KWin rules.
- `dev-qt/qtmultimedia:6`, `dev-qt/qtwebsockets:6`,
  `dev-qt/qtwebengine:6[widgets]`, `dev-qt/qtwebchannel:6`,
  `kde-frameworks/kwallet:6`, OpenSSL, and NSS are direct dependencies.
- NSS supplies `certutil`, which the custom-certificate-authority feature uses
  to populate the Qt WebEngine trust store. Normal TLS validation remains
  enabled.

If Portage reports a Qt or KDE Frameworks slot conflict, complete the regular
system update before retrying the app; do not mask individual KDE packages to
force the install.
