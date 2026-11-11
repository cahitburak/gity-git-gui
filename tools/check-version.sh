#!/usr/bin/env bash
# Checks that every file naming Gity's version names the same one.
#
#   tools/check-version.sh            # they agree with each other
#   tools/check-version.sh 0.2.0      # and with this version (a tag, in CI)
#
# CMakeLists.txt's project(VERSION) is the source of truth: it is what the
# binary reports in --version and Help ▸ About. The others are written by
# hand, and drifted before this existed.
set -euo pipefail
cd "$(dirname "$0")/.."

cmake=$(sed -n 's/^ *VERSION \([0-9][0-9.]*\)$/\1/p' CMakeLists.txt | head -1)
vcpkg=$(sed -n 's/^ *"version": "\([^"]*\)".*/\1/p' vcpkg.json | head -1)
metainfo=$(sed -n 's/.*<release version="\([^"]*\)".*/\1/p' \
    packaging/io.github.cahitburak.gity_git_gui.metainfo.xml | head -1)
changelog=$(sed -n 's/^## \([0-9][0-9.]*\) .*/\1/p' CHANGELOG.md | head -1)
pkgbuild=$(sed -n 's/^pkgver=\(.*\)$/\1/p' packaging/arch/gity/PKGBUILD | head -1)

expected="${1:-$cmake}"
status=0
for pair in "CMakeLists.txt:$cmake" "vcpkg.json:$vcpkg" "metainfo (newest release):$metainfo" \
            "CHANGELOG.md (newest entry):$changelog" "packaging/arch/gity/PKGBUILD:$pkgbuild"; do
    file="${pair%%:*}"
    version="${pair##*:}"
    if [ "$version" != "$expected" ]; then
        echo "version mismatch: $file says '${version}', expected '${expected}'" >&2
        status=1
    fi
done
[ "$status" -eq 0 ] && echo "every file says ${expected}"
exit "$status"
