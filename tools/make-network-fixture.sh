#!/usr/bin/env bash
#
# Builds a clone and a bare "remote" on local disk, already diverged, so the
# network verbs can be exercised for real.
#
# The point is that this needs no network and no credentials: a bare repo on a
# path drives the same code in git that a real remote does, so fetch, pull and
# push all take their genuine paths — including the failures, which are the
# ones worth testing. Real repositories and real remotes are nobody's test
# fixture.
#
# Leaves the clone 1 ahead and 2 behind, which is the state that exercises the
# ahead/behind counts on the toolbar, the --ff-only refusal on pull, and the
# non-fast-forward rejection on push.
#
#   usage: tools/make-network-fixture.sh [output-dir]

set -euo pipefail

out="${1:-/tmp/gity-network-fixture}"
rm -rf "$out"
mkdir -p "$out"

remote="$out/remote.git"
seed="$out/seed"
clone="$out/clone"

# -b main on the bare repo too: if HEAD points at master while the seed pushes
# main, the clone comes out empty and every later step lies about why.
git init -q --bare -b main "$remote"

git init -q -b main "$seed"
git -C "$seed" config user.email fixture@gity.invalid
git -C "$seed" config user.name "Gity Fixture"
mkdir -p "$seed/Assets" "$seed/ProjectSettings"
echo "m_EditorVersion: 6000.3.16f1" > "$seed/ProjectSettings/ProjectVersion.txt"
echo one > "$seed/Assets/a.txt"
git -C "$seed" add -A
git -C "$seed" commit -qm "Initial commit"
git -C "$seed" remote add origin "$remote"
git -C "$seed" push -q -u origin main

git clone -q "$remote" "$clone"
git -C "$clone" config user.email fixture@gity.invalid
git -C "$clone" config user.name "Gity Fixture"

# Two commits only the remote has.
for n in 1 2; do
    echo "remote work $n" >> "$seed/Assets/a.txt"
    git -C "$seed" commit -qam "Remote work $n"
done
git -C "$seed" push -q

# One commit only the clone has.
echo "local work" > "$clone/Assets/local.txt"
git -C "$clone" add -A
git -C "$clone" commit -qm "Local work"
git -C "$clone" fetch -q

echo "remote: $remote"
echo "clone:  $clone"
echo -n "state:  "
git -C "$clone" status -sb | head -1
