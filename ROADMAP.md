# Roadmap

What is still open. Decisions and their reasons are in [ARCHITECTURE.md](ARCHITECTURE.md); what
exists is in the [README](README.md).

## Testing — the first priority

Gity has been used by one person, on Linux, against a handful of repositories. Before anything
else it needs:

- [ ] Wider use on Linux: different distributions, desktop environments and git versions.
- [ ] **macOS and Windows** actually used, not only built. CI builds both, on demand.
- [ ] Credential flows against real providers: GitHub, GitLab, Bitbucket, Gitea, Azure DevOps,
      over HTTPS and SSH, with and without GitHub CLI.
- [ ] Large and unusual repositories: very long histories, many submodules, LFS-heavy projects,
      sparse checkouts, worktrees.
- [ ] High-DPI and fractional scaling; screen readers (the refs sidebar and staging lists need a
      proper accessibility model per row).

## Release

- [ ] **AppImage**, via `linuxdeploy` and `linuxdeploy-plugin-qt`, built in CI on a tag.
      Bundling Qt brings the LGPL obligations described in
      [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
- [ ] **macOS DMG and Windows installer.** Both need signing to avoid system warnings: an Apple
      Developer ID, and a Windows certificate — possibly through an open-source signing
      programme such as SignPath.
- [ ] Release notes and versioned tags.

## Features

- [ ] **Browser sign-in, finished.** The device flow is built and tested against a stub; using it
      needs an OAuth client ID registered per host (GitHub: Settings ▸ Developer settings ▸ OAuth
      Apps, with device flow enabled). A PKCE loopback flow would remove the code-typing step.
- [ ] **Notice changes made outside Gity** — a file-system watcher, so edits in an editor or commits
      in a terminal show up without a manual refresh (ARCHITECTURE.md, ADR-004).
- [ ] Faster status on large working copies — libgit2's is single-threaded (ARCHITECTURE.md,
      *Risks*).
- [ ] Syntax highlighting in diffs — KSyntaxHighlighting or tree-sitter (ARCHITECTURE.md, open
      question 1).
- [ ] A conflict-resolution editor.
- [ ] Blame, and history for a single file or folder.
- [ ] A denser mode beyond the history rows, and further layout options.
