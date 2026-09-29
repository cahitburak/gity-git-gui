#!/usr/bin/env bash
# Builds the source archive attached to a GitHub release: gity-<version>.tar.gz.
#
#   tools/make-release-archive.sh 0.1.0 [output-dir]
#
# Reproducible: it archives the tagged commit's *tree* with a fixed timestamp,
# so the same tag always gives a byte-identical file and the checksum in the
# Arch PKGBUILD (packaging/arch/gity) can be written before the file is
# uploaded. packaging/arch is left out (.gitattributes, export-ignore), since a
# recipe cannot contain the checksum of an archive that contains it.
set -euo pipefail
version="${1:?usage: $0 <version> [output-dir]}"
out="${2:-.}"
ref="v${version}"
git rev-parse --verify --quiet "${ref}^{commit}" >/dev/null || ref=HEAD
date=$(git log -1 --format=%cd --date=format:'%Y-%m-%d 00:00:00 +0000' "$ref")
file="${out}/gity-${version}.tar.gz"
git archive --format=tar --prefix="gity-${version}/" --mtime="$date" "${ref}^{tree}" | gzip -9n > "$file"
echo "$file"
sha256sum "$file" | cut -d' ' -f1
