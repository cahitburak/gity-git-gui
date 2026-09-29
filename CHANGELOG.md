# Changelog

## 0.1.0 — 2026-09-29

The first public release of Gity: a cross-platform Git client in C++20 and Qt 6. It has not been
widely tested — see the status note in the [README](README.md).

Highlights:

* A streamed, custom-painted commit graph, with filters by text, author and date.
* A sidebar that marks the current and the default branch, flags branches deleted on the remote,
  and lets any branch be pinned; Ctrl-click two branches to compare them.
* Staging by file, hunk and line; commit, amend, and commit reasons shown when disabled.
* Undo and redo for commits, merges, rebases, resets, checkouts, deleted branches and tags,
  dropped stashes, discards and pulls.
* Previews before pull, merge, rebase and push; force push only with a lease.
* Merge, cherry-pick, revert, stash, and plain and interactive rebase — with reword, and
  stop-to-edit in Local Changes.
* Submodules as repositories in their own tabs; fetch includes them.
* Checkout and new branch ask what to do with uncommitted changes, and bring submodules along.
* Pull or merge request pages opened on GitHub, GitLab, Bitbucket, Gitea and Azure DevOps.
* Credentials through git's own helper — Gity stores none — with per-repository accounts and
  GitHub CLI support.
* LFS locks, image diffs, a command log, and seven themes plus one that follows the desktop.
