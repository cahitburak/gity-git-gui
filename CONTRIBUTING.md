# Contributing to Gity

Thanks for looking. Gity is a personal project and still young (see the status note in the
[README](README.md)), so issues describing what broke for you are as useful as code.

## Before you start

* **Read [ARCHITECTURE.md](ARCHITECTURE.md).** The ADRs explain decisions that look odd until you
  know why — writes go through the `git` binary (ADR-003), repository work runs on a worker thread
  (ADR-004), Gity stores no credentials (ADR-010), and every destructive action is undoable
  (ADR-011).
* For anything larger than a fix, open an issue first so we can agree on the shape.
* Security problems go through [SECURITY.md](SECURITY.md), not public issues.

## Building and testing

```bash
cmake --preset dev && cmake --build --preset dev && ctest --preset dev
```

Before sending a pull request, build the `ci` preset — it treats warnings as errors, which is
what CI runs:

```bash
cmake --preset ci && cmake --build --preset ci && ctest --preset ci
```

`ci-headless` builds only `src/core` and its tests, with no Qt at all; it is how the layering rule
below is enforced. `dev-asan` adds address and undefined-behaviour sanitizers.

## The rules the code keeps

* **Layers.** `src/core` has no Qt and no display; `src/session` uses QtCore only; `src/ui` and
  `src/app` are Qt Widgets. Tests link `core` only — if a test needs QtWidgets, the logic belongs
  in `core`.
* **Every write goes through `git`** via `GitProcess` (`src/session/GitProcess.h`), never libgit2,
  so hooks, filters, LFS and the user's config behave exactly as in a terminal.
* **Nothing touching a repository runs on the UI thread.** Ask the session; it answers with a
  signal.
* **No colour literals outside the theme layer.** Colours come from `GityDesign/TOKENS.json` via
  the generated `Tokens.h`, and chrome is styled from `src/ui/theme/gity.qss`. Regenerate tokens
  with `tools/generate-tokens.py`.
* **Destructive actions are undoable** (record them with the undo log) **or say plainly that they
  are not**, and ask first.
* **Say what a button does.** New menu entries and buttons get a tooltip naming the git command
  behind them (`describeCommand`).
* **Colour is never the only signal.** Every coloured state also carries a word or a shape.
* **Tests** for anything in `core`, especially parsing of git output: add the real output you saw
  as a fixture.

## Style

`.clang-format` is the formatting authority. Comments explain *why*, not what — the code already
says what. Commit messages: a short imperative summary, then a body saying what changed and why.

## Traps worth knowing

Each of these has bitten this code once; they are why some things are done the long way.

* **A path is a pattern unless you say otherwise.** git and libgit2 read path arguments as
  pathspecs, so `[id].tsx` also matches `d.tsx`. Use `--literal-pathspecs` for the git CLI and
  `GIT_DIFF_DISABLE_PATHSPEC_MATCH` for libgit2 diffs.
* **`--continue` opens an editor** nobody can see. History edits run with `GIT_EDITOR=true`, and
  every git process gets `/dev/null` as stdin.
* **Undo must never move a ref someone else moved.** `planRefRestore` only restores a ref that
  still holds what the operation left in it; "set everything back" would delete commits made since.
* **`QAction::triggered(bool)` into a slot whose first parameter is a bool** passes `checked`
  (false) into it. Connect through a lambda.
* **An inline stylesheet or a pixmap icon freezes the colours of the moment** and ignores theme
  changes. Style long-lived widgets from `gity.qss` by object name or role (`applyRole`), and use
  `icons::themed()` for glyphs.
* **Repository paths arrive spelled more than one way** — libgit2 adds a trailing slash. Compare
  them through `RepositoryTabs::canonical`.
* **`git switch` leaves submodules where they were.** Only `git submodule update` moves them;
  checkout offers it.
* **A QSplitter pane stops at its content's minimum size, then collapses to nothing.** Named
  splitters are non-collapsible with explicit small floors, reset after every `restoreState`.
* **GitHub CLI answers for one account, and only by that name.** If a remote URL names a different
  user than the active `gh` account, git gets no token. `findAccountMismatch` explains this to the
  user.
* **`git rebase --continue` refuses staged changes at an edit stop** once HEAD has moved. Gity's
  Continue commits them first.

## Licence

By contributing you agree that your contribution is licensed under the project's
[MIT licence](LICENSE).
