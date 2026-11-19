# Changelog

## Unreleased

A review of the whole codebase, and what it found fixed. Details are in the commit messages and
[ROADMAP.md](ROADMAP.md).

* **Staging goes through `git`.** It used libgit2's index, which runs no external clean filter: a
  file under LFS was staged whole and one under git-crypt in plaintext. Partial staging also
  works now on CRLF files, on new files, when unstaging part of a hunk, and on deleted lines that
  start with `-- `.
* **Undo can be trusted.** A merge finished in a terminal no longer absorbs the next commit; an
  undo that cannot finish changes nothing; the log is written atomically, shared safely between
  windows, and kept when a deleted annotated tag is garbage-collected.
* **One worker per repository.** A preview, a fetch or a status from one tab can no longer act on
  or appear in another, and a long fetch no longer blocks opening another repository. Network
  commands can be cancelled; quitting during a commit or rebase asks, and waits for it.
* **Changes made outside Gity are noticed** — edits, commits and checkouts — on Linux as they
  happen, elsewhere when the window is brought forward.
* **Tokens stay off screen** in the command log, git's errors, the remotes dialog and submodule
  tooltips; prompts, commit subjects, branch, tag and file names and what a server says back are
  shown as text, never as markup.
* Crashes fixed: filtered history, an absurd progress line from a server, and two dialogs that held
  pointers into data a refresh could replace.
* The sidebar, the welcome screen's recent list and the commit menu work from the keyboard, and
  staging rows are read out by screen readers.
* Many smaller fixes: `git am` and multi-commit cherry-picks recognised, binary files shown as
  binary, file status colours, the diff keeping its place after staging, reword keeping lines that
  start with `#`, and more.
* CI packages releases (AppImage, macOS disk image, Windows zip) into a draft release on a tag, and
  checks sanitizers, versions, desktop metadata and formatting. Tests now include real
  repositories driven through `git`: 366 in all, up from 187.
* **A second review, of the branch as a whole.**
  * Credentials: a token used as the user name stays masked, a remote URL cannot inject lines into
    the credential helper, and a working-copy path cannot read or write outside the repository
    through `..` or a symlinked folder.
  * Names starting with `-` can no longer be taken for options (`git branch -- …`), and an undo
    no longer restores another branch's files when HEAD moved in a terminal, or touches the parent
    repository for an uninitialised submodule.
  * Partial staging next to a last line without a newline, of a deleted file, and of paths with
    quotes or control characters produces patches git accepts.
  * A file's history keeps commits from before a rename on repositories with other branches, and
    counts a commit that only changed a file's mode.
  * Conflicts in submodules and symlinks open as a choice of side, and keeping a submodule's side
    stages that side's commit.
  * Opening a repository through a subfolder no longer duplicates its tab, and opening a folder with
    no repository no longer crashes.
  * Conflict buttons stay clickable after part of the editor repaints, the columns scroll as one
    in both layouts, and an edit in progress counts as unsaved.
  * Highlighting leaves very long lines plain instead of freezing the window.
  * The push preview describes a fork's push when `pushRemote` sends it there.
* **A third review, of the second one's fixes.**
  * A link in the working copy into `.git`, or one leading nowhere, can no longer be written
    through when a conflict resolution is saved.
  * Reopening a repository by the subfolder or symlink it was first opened through switches to
    its tab, instead of opening a second one whose closing emptied the first; closing such a
    duplicate no longer clears the repository's conflict choices.
  * Undoing a discard, stash or tag after a commit made in a terminal leaves that commit's files
    alone.
  * Conflict writes that finish after their tab is left are settled for their own repository, and
    a cancelled quit opens the conflict window on the unsaved file, not the first one.
  * A second Cancel is no longer undone by the first, closing a tab during a preview no longer
    blocks the window, and a cancel during the last submodule's fetch says Stopped.
  * A sign-in reply whose token is not a string is not taken as granted.
* The two largest files are split up — the main window and the repository session — with no change
  in behaviour.
* **History of a file or folder, and blame.** Right-click a file in a commit, a comparison or the
  working copy — or use the Repository menu — to list only the commits that changed it, or to see
  who last changed each line. Blame opens in a third tab under the graph; a click on a line selects
  its commit, and the version before any change is a right-click away. Blaming the working copy
  marks lines not committed yet. A file's history follows it through renames, and the rows from
  before one say what it was called then.
* **A conflict editor.** Ours, theirs and the result, aligned region by region, in three columns
  with the result in the middle, or two with the result below them — chosen at the top and
  remembered. Take a side, both, or neither for each conflict, or edit the result in place; Save
  and Mark Resolved stages it, and Start Over brings the conflict back. Binary files and files one
  side deleted are resolved by keeping a whole version. Open it from the banner during a merge or
  rebase, by double-clicking a conflicted file, or from Repository ▸ Resolve Conflicts….
* **Syntax highlighting** in diffs, blame and the conflict editor, in light and dark to match the
  theme. Each file is coloured whole, so a hunk that starts inside a comment shows as one. It uses
  KSyntaxHighlighting where it is installed; without it code is drawn plain.

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
