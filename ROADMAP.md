# Roadmap

What is still open. Decisions and their reasons are in [ARCHITECTURE.md](ARCHITECTURE.md); what
exists is in the [README](README.md).

## Correctness and safety — before anyone else uses it

Done in November 2026, on the `review-fixes` branch; kept here until it is released, as the
record of what changed and why. A full review of the code found places where Gity does not yet keep the promises
the README makes: that every write goes through `git`, and that undo can be trusted. These come
before wider testing, because the first testers should meet portability problems, not data-safety
ones.

- [x] **Every index write through `git`.** Staging used libgit2's index, which runs no external
      clean filter: an LFS file went into the index whole, and a git-crypt file in plaintext.
      Partial staging also broke on CRLF files, on new files, on partial unstage and on deleted
      lines starting with `-- `. Stage, unstage and patch application go through `git add` and
      `git apply`, with the patch passed as bytes.
- [x] **A trustworthy undo log.** A stale pending entry must not swallow the next operation;
      `undo.json` is written atomically and locked; restore checks before it moves anything; an
      interrupted operation is not recorded as finished; deleted annotated tags stay reachable;
      discard and abort are always recorded.
- [x] **No crashes from bad input.** Filtered history rows initialised; server progress text parsed
      without exceptions; no pointer into a model held across a confirmation dialog.
- [x] **No token on screen.** Credentials in URLs masked in the command log, git's error output and
      the remotes dialog; prompts and commit subjects shown as plain text, never as markup.
- [x] **One worker per repository**, as ADR-004 says, rather than one per window: a result can no
      longer land in the wrong tab, and a long fetch no longer blocks opening another repository.
      Quitting during an operation asks first; a running git command can be cancelled; no git runs
      on the UI thread.
- [x] **Tests that use real repositories** for staging, history streaming, status and the undo
      log, and a test target for `src/session` (QtCore only).
- [x] **CI that guards the rules**: a sanitizer job, a clang-format check, appstream and desktop-file
      validation.
- [x] **No class too big to review.** `MainWindow.cpp` (3,100 lines) and `RepoSession.cpp`
      (2,700) are split: the worker has its own header and its code is split by area; the window's
      sessions, history filters, switch dialogs, preview wording and saved state are classes of
      their own, the ones that need no widgets tested in `tests/session`.

## Testing

Gity has been used by one person, on Linux, against a handful of repositories. Once the section
above is done, it needs:

- [ ] Wider use on Linux: different distributions, desktop environments and git versions.
- [ ] **macOS and Windows** actually used, not only built. CI builds both, on demand.
- [ ] Credential flows against real providers: GitHub, GitLab, Bitbucket, Gitea, Azure DevOps,
      over HTTPS and SSH, with and without GitHub CLI.
- [ ] Large and unusual repositories: very long histories, many submodules, LFS-heavy projects,
      sparse checkouts, worktrees.
- [ ] High-DPI and fractional scaling; screen readers (the refs sidebar and staging lists need a
      proper accessibility model per row); every action reachable from the keyboard.

## Release

- [ ] **AppImage**, via `linuxdeploy` and `linuxdeploy-plugin-qt`, built in CI on a tag. The job
      exists (an Ubuntu 20.04 container, licences bundled) but has not run: the first tag tries
      it.
- [ ] **macOS DMG and Windows installer.** CI builds an unsigned disk image and a portable zip on a
      tag, and signs and notarizes the disk image once the Apple secrets are set. Still needed:
      an Apple Developer ID, a Windows installer, and a Windows certificate — possibly through an
      open-source signing programme such as SignPath — and universal2 macOS builds.
- [x] Release notes and versioned tags: CI runs on `v*` tags and attaches the artifacts to a draft
      release with the CHANGELOG entry as notes; `tools/check-version.sh` keeps one version
      everywhere, and Help ▸ About shows it.
- [x] `gity-askpass` inside the macOS bundle, where `GitProcess` looks for it.
- [x] THIRD_PARTY_NOTICES updated for what each kind of package bundles.

## Features

- [ ] **Browser sign-in, finished.** The device flow is built and tested against a stub; using it
      needs an OAuth client ID registered per host (GitHub: Settings ▸ Developer settings ▸ OAuth
      Apps, with device flow enabled). A PKCE loopback flow would remove the code-typing step.
- [x] **Notice changes made outside Gity** — HEAD, the index and refs on every platform, and the
      working tree with inotify on Linux (ARCHITECTURE.md, ADR-004).
- [ ] **Native working-tree watchers on macOS and Windows** — FSEvents and
      `ReadDirectoryChangesW`. Until then an edit there is noticed when the window is brought back
      to the front.
- [ ] Faster status on large working copies — libgit2's is single-threaded (ARCHITECTURE.md,
      *Risks*).
- [ ] Syntax highlighting in diffs — KSyntaxHighlighting or tree-sitter (ARCHITECTURE.md, open
      question 1).
- [ ] A conflict-resolution editor.
- [x] **Blame, and history for a single file or folder** — from the file lists' menus and the
      Repository menu. A file's history follows it through renames.
- [ ] A denser mode beyond the history rows, and further layout options.
