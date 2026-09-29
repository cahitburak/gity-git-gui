# Packaging

`cmake --install` lays the application out under a prefix, and CPack turns that into an archive.
Both work with no network access and no downloads.

    cmake --preset ci
    cmake --build --preset ci
    cmake --install build/ci --prefix /tmp/stage
    cd build/ci && cpack -G TGZ

An install contains:

| Path | What |
|---|---|
| `bin/gity`, `bin/gity-askpass` | The application and its credential-prompt helper |
| `share/applications/io.github.cahitburak.gity_git_gui.desktop` | The desktop entry |
| `share/icons/hicolor/scalable/apps/io.github.cahitburak.gity_git_gui.svg` | The icon |
| `share/metainfo/io.github.cahitburak.gity_git_gui.metainfo.xml` | AppStream metadata — what software centres show |
| `share/doc/Gity/` | `LICENSE`, `THIRD_PARTY_NOTICES.md`, `README.md` |

`io.github.cahitburak.gity_git_gui` is the application's permanent ID: reverse-DNS under the
project's own GitHub address, as AppStream and Flathub require. Do not change it — software centres
and desktops identify the app by it.

Check the metadata after any change to it:

    desktop-file-validate packaging/io.github.cahitburak.gity_git_gui.desktop
    appstreamcli validate --pedantic packaging/io.github.cahitburak.gity_git_gui.metainfo.xml

## Arch Linux and CachyOS (AUR)

`arch/gity` builds a tagged release; `arch/gity-git` builds the latest `main`. Both are the recipes
published to the AUR, kept here so they change with the code. They are left out of source archives
(`.gitattributes`), because a recipe records the checksum of the archive it builds from.

**Making a release**

1. Set the version in `CMakeLists.txt` and `vcpkg.json`, add a CHANGELOG entry and a `<release>` to
   the metainfo file, and commit.
2. Tag the commit `v<version>` — signed, if you can (`git tag -s`) — and push the tag.
3. Build the source archive. It is reproducible: the same tag always gives the same bytes.

       tools/make-release-archive.sh <version>

   It prints the file and its SHA-256.
4. Create the GitHub release for the tag and attach `gity-<version>.tar.gz`.
5. In `arch/gity/PKGBUILD`, set `pkgver`, reset `pkgrel=1`, and put the printed SHA-256 in
   `sha256sums`.
6. Test it: `makepkg -f` in a copy of `arch/gity` — or, better, in a clean chroot with
   `extra-x86_64-build` from `devtools` — and lint it with `namcap PKGBUILD` and
   `namcap gity-*.pkg.tar.zst`.
7. Regenerate `.SRCINFO` with `makepkg --printsrcinfo > .SRCINFO`, and push the `PKGBUILD` and
   `.SRCINFO` to `ssh://aur@aur.archlinux.org/gity.git`.

`gity-git` needs no change per release: its version is worked out from the latest tag when it
builds.

## What is here and what is not

The CPack archive depends on the system Qt, which is right for a distribution package and wrong for
something a person downloads from a release page.

A self-contained **AppImage** needs `linuxdeploy` and `linuxdeploy-plugin-qt`, which are fetched as
binaries from their GitHub releases and run locally. That is a deliberate omission rather than an
oversight: pulling an executable off the internet to run during a build is a decision for whoever
owns the release process, not something to add quietly. When you want it, the two plugins go in a
CI job that runs on a tag, alongside the macOS and Windows legs.

**macOS DMG** and the **Windows installer** additionally need signing identities — see
[ROADMAP.md](../ROADMAP.md).

## Licences in a binary package

A package that bundles Qt or libgit2 must carry their licences.
[THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md) says what each needs; in practice:

* Put `LICENSE`, `THIRD_PARTY_NOTICES.md`, Qt's `LICENSES/` (LGPL-3.0, GPL-3.0) and libgit2's
  `COPYING` in the package — under `share/doc/Gity/` on Linux, where `cmake --install` already puts
  `LICENSE` and the notices; in `Contents/Resources/` in a macOS bundle; beside the executable on
  Windows. Arch packages also install `LICENSE` to `/usr/share/licenses/<pkgname>/`, as the
  PKGBUILDs here do.
* Keep Qt as shared libraries (linuxdeploy, macdeployqt and windeployqt all do), and link the exact
  Qt source release used from the release notes.
* With vcpkg, add each port's `share/<port>/copyright`.

## The askpass helper

`gity-askpass` installs next to `gity` and must stay there: the application locates it relative to
its own executable and never from `PATH`, because a credential prompt is the last place to run
whatever happens to carry that name. Any packaging that separates them silently disables credential
prompting.
