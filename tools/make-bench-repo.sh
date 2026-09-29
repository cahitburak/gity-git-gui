#!/usr/bin/env bash
#
# Builds a synthetic repository with realistic lane structure so the M0
# benchmark has something with volume to chew on.
#
# This is a smoke target, NOT the M0 gate. The gate in the architecture record
# is a *real Unity repository with LFS* — synthetic history has uniform object
# sizes, no binary assets, no LFS pointers and a tidy worktree, so it flatters
# libgit2 status in exactly the way the risk register warns about.
#
#   usage: tools/make-bench-repo.sh [commits] [output-dir]

set -euo pipefail

COMMITS="${1:-50000}"
OUT="${2:-$(pwd)/bench-repo}"

rm -rf "$OUT"
mkdir -p "$OUT"
git -C "$OUT" init -q -b main

# Mostly linear history, with a 3-commit side branch merging back every 20
# commits — enough churn to exercise lane opening, pass-through and closing.
awk -v n="$COMMITS" '
function emit(ref, m, p1, p2, body,    msg) {
    msg = "commit " m
    printf "commit %s\n", ref
    printf "mark :%d\n", m
    printf "author %s %d +0000\n", who, ts
    printf "committer %s %d +0000\n", who, ts
    printf "data %d\n%s\n", length(msg), msg
    if (p1 > 0) printf "from :%d\n", p1
    if (p2 > 0) printf "merge :%d\n", p2
    printf "M 100644 inline file%d.txt\n", m % 500
    printf "data %d\n%s\n", length(body), body
}
BEGIN {
    who = "bench <bench@example.invalid>"
    ts = 1000000000
    mark = 0
    prev = 0
    for (i = 1; i <= n; i++) {
        if (i % 20 == 0 && prev > 0) {
            side = prev
            for (j = 0; j < 3; j++) {
                emit("refs/heads/side", ++mark, side, 0, "side " i "." j)
                side = mark
            }
            emit("refs/heads/main", ++mark, prev, side, "merge " i)
        } else {
            emit("refs/heads/main", ++mark, prev, 0, "main " i)
        }
        prev = mark
        ts += 60
    }
}' | git -C "$OUT" fast-import --quiet

git -C "$OUT" symbolic-ref HEAD refs/heads/main
git -C "$OUT" reset --hard -q main

printf 'created %s with %s commits (%s objects)\n' \
    "$OUT" \
    "$(git -C "$OUT" rev-list --count --all)" \
    "$(git -C "$OUT" count-objects -v | awk '/^count:/{print $2}')"
