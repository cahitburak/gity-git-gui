# Roadmap

What is still open. Decisions and their reasons are in [ARCHITECTURE.md](ARCHITECTURE.md); what
exists is in the [README](README.md).

## Correctness and safety — before anyone else uses it

A full review of the code (November 2026) found places where Gity does not yet keep the promises
the README makes: that every write goes through `git`, and that undo can be trusted. These come
before wider testing, because the first testers should meet portability problems, not data-safety
ones.

- [ ] **Every index write through `git`.** Staging used libgit2's index, which runs no external
      clean filter: an LFS file went into the index whole, and a git-crypt file in plaintext.
      Partial staging also broke on CRLF files, on new files, on partial unstage and on deleted
      lines starting with `-- `. Stage, unstage and patch application go through `git add` and
      `git apply`, with the patch passed as bytes.
- [ ] **A trustworthy undo log.** A stale pending entry must not swallow the next operation;
      `undo.json` is written atomically and locked; restore checks before it moves anything; an
      interrupted operation is not recorded as finished; deleted annotated tags stay reachable;
      discard and abort are always recorded.
- [ ] **No crashes from bad input.** Filtered history rows initialised; server progress text parsed
      without exceptions; no pointer into a model held across a confirmation dialog.
- [ ] **No token on screen.** Credentials in URLs masked in the command log, git's error output and
      the remotes dialog; prompts and commit subjects shown as plain text, never as markup.
- [ ] **One worker per repository**, as ADR-004 says, rather than one per window: a result can no
      longer land in the wrong tab, and a long fetch no longer blocks opening another repository.
      Quitting during an operation asks first; a running git command can be cancelled; no git runs
      on the UI thread.
- [ ] **Tests that use real repositories** for staging, history streaming, status and the undo
      log, and a test target for `src/session` (QtCore only).
- [ ] **CI that guards the rules**: a sanitizer job, a clang-format check, appstream and desktop-file
      validation.

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

- [ ] **AppImage**, via `linuxdeploy` and `linuxdeploy-plugin-qt`, built in CI on a tag.
      Bundling Qt brings the LGPL obligations described in
      [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
- [ ] **macOS DMG and Windows installer.** Both need signing to avoid system warnings: an Apple
      Developer ID, and a Windows certificate — possibly through an open-source signing
      programme such as SignPath.
- [ ] Release notes and versioned tags. CI runs on `v*` tags and attaches the artifacts to a draft
      release; one version number, from `CMakeLists.txt`, shown in Help ▸ About.
- [ ] `gity-askpass` inside the macOS bundle, where `GitProcess` looks for it — without it,
      credential prompts are silently off on macOS.
- [ ] THIRD_PARTY_NOTICES updated for what a bundled package actually ships (libgit2 and its
      dependencies, the libraries Qt bundles).

## Features

- [ ] **Browser sign-in, finished.** The device flow is built and tested against a stub; using it
      needs an OAuth client ID registered per host (GitHub: Settings ▸ Developer settings ▸ OAuth
      Apps, with device flow enabled). A PKCE loopback flow would remove the code-typing step.
- [ ] **Notice changes made outside Gity** — a file-system watcher, so edits in an editor or commits
      in a terminal show up without a manual refresh (ARCHITECTURE.md, ADR-004). Needs one worker
      per repository first.
- [ ] Faster status on large working copies — libgit2's is single-threaded (ARCHITECTURE.md,
      *Risks*).
- [ ] Syntax highlighting in diffs — KSyntaxHighlighting or tree-sitter (ARCHITECTURE.md, open
      question 1).
- [ ] A conflict-resolution editor.
- [ ] Blame, and history for a single file or folder.
- [ ] A denser mode beyond the history rows, and further layout options.
