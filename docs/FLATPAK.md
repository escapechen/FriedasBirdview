# Flatpak package

The primary cross-distribution package will use Flatpak. The local manifest is
at `packaging/flatpak/io.github.escapechen.friedasbirdview.yml` and uses the
KDE 6.10 runtime with Flathub's Qt WebEngine base app.

The Flatpak application ID is `io.github.escapechen.friedasbirdview`. It is
intentionally distinct from the native package's desktop ID so that Flatpak
settings, WebEngine data, and custom certificate database remain in its own
sandbox.

## Install a release bundle

Each GitHub version tag builds an x86_64 bundle and attaches it to the matching
GitHub Release. Download `friedasbirdview-x86_64.flatpak` from the latest
release, then install it locally:

```sh
flatpak install --user ./friedasbirdview-x86_64.flatpak
flatpak run io.github.escapechen.friedasbirdview
```

Flatpak bundles are not a GitHub Packages registry format, so the downloadable
release asset is the immediate distribution channel. Flathub remains the
planned discoverable, updateable distribution channel.

The workflow at `.github/workflows/flatpak-release.yml` runs automatically for
new `v*` tags. It can also be started from **Actions → Publish Flatpak bundle →
Run workflow** and given an existing tag that already contains the Flatpak
manifest, which is useful for re-publishing a release asset.

## Local build

Install `flatpak-builder` using your distribution's package manager, then add
Flathub if it is not already configured:

```sh
flatpak remote-add --user --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo
flatpak install --user flathub org.kde.Sdk//6.10 io.qt.qtwebengine.BaseApp//6.10
```

From the repository root, build, install, and run the current checkout:

```sh
flatpak-builder --force-clean --user --install-deps-from=flathub \
  --repo=flatpak-repo flatpak-build \
  packaging/flatpak/io.github.escapechen.friedasbirdview.yml
flatpak run io.github.escapechen.friedasbirdview
```

To remove the local test build later:

```sh
flatpak uninstall --user io.github.escapechen.friedasbirdview
rm -rf flatpak-build flatpak-repo
```

## Permissions and limitations

The manifest grants only network, graphics, audio, X11, the Plasma
StatusNotifier watcher, and KDE Wallet. It does not grant access to the host
home directory: the certificate picker uses the document portal, while
settings, the custom certificate database, and the ephemeral WebEngine profile
stay in Flatpak's per-app data directory. The picker starts in Downloads; the
portal lets the user select a public CA from `/etc/ssl/certs` or another host
location without exposing the whole directory tree to the application. If a
desktop’s chooser cannot browse a protected location, copy the public CA to
Downloads first. The package includes the small NSS `certutil` companion
required to add and remove custom CAs from WebEngine's private trust database;
no host certificate tool is needed.

The current overlay relies on X11/Xwayland to restore its saved position. The
Flatpak consequently requests the X11 socket and does not force a native
Wayland overlay.

The native build creates an XDG autostart entry. The Flatpak build uses the
desktop's Background portal instead: enabling the switch asks the desktop for
approval to start FriedasBirdview after sign-in. If the host desktop does not
provide that portal, the switch explains the requirement rather than silently
failing.

Before a public Flathub submission, verify the custom-CA import and KWallet
operations with `flatpak run --log-session-bus
io.github.escapechen.friedasbirdview`. Add only the exact additional D-Bus
name, if any, shown as required. Do not grant a full session bus or home
directory access.

## Flathub submission

Flathub builds from an immutable source archive, not the local directory
source used above. After publishing a signed version tag and making the
repository public, create the Flathub manifest from this one by replacing the
module source with the matching GitHub tag archive and its SHA-256 checksum.

Also add public, sanitized screenshots to the AppStream metadata before the
submission. The existing development screenshots are not referenced by the
package metadata, so no private Frigate information is published implicitly.
