# Third-party notices

Gity's own source code is under the MIT licence ([LICENSE](LICENSE)). It is built on, and at
run time uses, the software below. None of it is copied into this repository; each is obtained
from its own distribution when Gity is built or run.

This file is a summary, not legal advice. The licence texts themselves are authoritative; each
is linked below and ships with the component.

## Linked into the application

| Component | Used for | Licence |
|---|---|---|
| [Qt 6](https://www.qt.io/) — Core, Gui, Widgets, Network, DBus (Linux), and the SVG image-format plugin at run time | The whole user interface; D-Bus access to the system keyring on Linux | [LGPL-3.0-only](https://doc.qt.io/qt-6/lgpl.html) (also available under GPL and commercial terms) |
| [libgit2](https://libgit2.org/) | Reading repositories: history, refs, diffs, status | [GPL-2.0 with a linking exception](https://github.com/libgit2/libgit2/blob/main/COPYING) |

### Linked along with libgit2

libgit2 is built against these, and wherever libgit2 is bundled they are too:

| Component | Licence |
|---|---|
| [PCRE2](https://github.com/PCRE2Project/pcre2) | [BSD-3-Clause with PCRE2 exception](https://github.com/PCRE2Project/pcre2/blob/master/LICENCE.md) |
| [zlib](https://zlib.net/) | [zlib licence](https://zlib.net/zlib_license.html) |
| [OpenSSL](https://www.openssl.org/) (Linux, where libgit2 uses it for HTTPS) | [Apache-2.0](https://www.openssl.org/source/license.html) for 3.x; the OpenSSL and SSLeay licences for 1.1 |

libgit2 is built without SSH support — every network operation goes through `git` — so libssh2 is
not part of any Gity package.

Which packages bundle them:

* **Distribution packages** (`cmake --install`, CPack, the AUR) bundle nothing: Qt, libgit2 and
  these come from the system.
* **The AppImage** bundles Qt and libgit2 (built from source with PCRE2, zlib and the build
  system's OpenSSL 1.1), together with the libraries `linuxdeploy` collects for them. Those
  include libraries Qt itself uses — ICU, and image and text libraries such as libpng, FreeType
  and HarfBuzz where they are not on linuxdeploy's exclude list — each under its own permissive or
  LGPL licence; `usr/lib` inside the AppImage lists exactly which.
* **The macOS disk image** bundles Qt and Homebrew's libgit2 and its dependencies, through
  `macdeployqt`.
* **The Windows zip** bundles Qt and vcpkg's libgit2 and its dependencies; each vcpkg port's
  licence is in its `licences` folder.

## Used only to build or test (not distributed)

| Component | Licence |
|---|---|
| [GoogleTest](https://github.com/google/googletest) | [BSD-3-Clause](https://github.com/google/googletest/blob/main/LICENSE) |
| [CMake](https://cmake.org/), [Ninja](https://ninja-build.org/) | BSD-3-Clause, Apache-2.0 |
| [vcpkg](https://github.com/microsoft/vcpkg) | MIT |

## Run as separate programs (not linked, not distributed)

| Program | Why | Licence |
|---|---|---|
| [Git](https://git-scm.com/) | Every write, and every network operation, goes through the `git` on your system (ARCHITECTURE.md, ADR-003) | [GPL-2.0-only](https://github.com/git/git/blob/master/COPYING) |
| [Git LFS](https://git-lfs.com/) (optional) | LFS locks and pointers | [MIT](https://github.com/git-lfs/git-lfs/blob/main/LICENSE.md) |
| [GitHub CLI](https://cli.github.com/) (optional) | Listing and using its signed-in accounts | [MIT](https://github.com/cli/cli/blob/trunk/LICENSE) |

Running a program is not linking to it, so their licences place no terms on Gity.

## Artwork and fonts

The application icon and every toolbar and menu glyph are drawn for Gity and are covered by its
MIT licence. Gity bundles no fonts; it uses the system's.

## If you distribute binaries

The source repository needs nothing beyond this file. A binary package — an AppImage, a DMG,
an installer — that **bundles** Qt or libgit2 takes on their conditions:

* **Qt (LGPL-3.0):** ship the LGPL-3.0 and GPL-3.0 licence texts; state that the package uses Qt
  under the LGPL and which version; keep Qt as shared libraries so users can replace them with
  their own build (Gity already links Qt dynamically — keep it that way); and make the
  corresponding Qt source available, or a written offer for it, e.g. a link to the exact Qt
  release on download.qt.io.
* **libgit2 (GPL-2.0 with linking exception):** ship its `COPYING` file. The exception means the
  GPL does not extend to Gity for linking it, whether statically or dynamically.
* **The vcpkg libraries above:** ship their licence texts; vcpkg installs each one's copyright
  file under `share/<port>/copyright`.

`packaging/README.md` says where these go in each package.
