# SPEC — the visual specification

What goes where, and how it looks. Exact colours and metrics live in [`TOKENS.json`](./TOKENS.json)
and are referred to here by name — `text.dim`, `selection.marker` and so on. Themes override those
values, never the layout ([`THEMES.md`](./THEMES.md)). The code cites this file by section number,
§0 to §3.

**General rules**

* Chrome text is 12px; code is 11.5px monospace. History rows are 26px, or 22px in Compact.
* The window's minimum is **1024 × 700**, and its first size fits the screen. Panes are splitters
  with small floors: resizable, remembered, and never collapsing to nothing — a pane shrunk to its
  header can be restored by clicking the header.
* Keyboard focus is visible wherever it can land, and distinct from selection.
* Colour is never the only signal: every coloured state also carries a word or a shape. Text is
  at least 4.5:1 against the surface it sits on, in every theme.
* No transition animations. State changes are immediate; the only motion is progress feedback for
  long git operations.

---

## 0. Global chrome

Top to bottom: the menu bar, the toolbar, a band of repository tabs, then the sidebar beside the
main area, then the status bar. The window uses the native frame, titled
`<repository> — <branch> — Gity`.

### Menu bar

**File · Edit · View · Repository · Branch · Stash · Tools · Help**, 12px `text.muted`, hover on
`selection.controlHover`.

### Toolbar — 46px

Icon above label. Left to right: `Fetch` (with a menu arrow), `Pull` and `Push` — each with its
count when non-zero, the ahead count in `semantic.warn` — then a divider, then `Branch`, `Stash`,
`Merge`, `Rebase` (with a menu arrow). At the far right, apart from the git verbs, a gear:
`Settings`. Every button's tooltip names the git command behind it.

### Repository tabs

One tab per open repository, the first one included, on `surface.header`; the selected tab has a
2px `selection.marker` underline. Submodules open as tabs of their own.

### Status bar

`surface.header`, 11px. Left: the working directory and branch. Right: the command log button and
the commit count.

### Operation banner

When a merge, rebase, cherry-pick or revert is in progress, a warn-tinted banner above the main
area says so and offers **Continue** and **Abandon**. At a rebase's edit stop it explains that the
commit's changes are staged in Local Changes.

### Sidebar

`surface.panel`, 236px by default, scrolling vertically, with a branch filter field at the top.

Group headings use `type.sectionLabel` — small, uppercase, `text.labelDim`. Rows: a 13px glyph box,
an elided label (middle-elided, so both ends of `feature/…/name` stay readable), then a right-hand
badge chip on `fills.neutralChip`, radius 3.

| Group | Contents |
|---|---|
| *(the repository's name, full size)* | `Local Changes` with its change count, `All Commits`; a warn-toned `Detached HEAD` row when HEAD is detached |
| Pinned | Branches the user pinned, local or remote, in the order pinned |
| Branches | Local branches. Badges: `↑2 ↓1` against the upstream (ahead in `semantic.warn`), `local` when there is none |
| Remotes | One row per remote, its branches nested under it |
| Tags | Newest version first |
| Submodules | Path and recorded short id; warn-toned when checked out at another commit or modified |
| Stashes | `stash@{n}`, message on hover |

Row states:

* **Selected:** `selection.bg`, a 2px `selection.marker` bar at the left edge, `text.selectedPrimary`.
* **Hover:** `selection.hover`.
* **Current branch:** a ✓ in `accent.primary` instead of the branch glyph, an `accent.primary`
  tint across the row, and demi-bold `text.strong`.
* **Default branch** (what the remote's HEAD names, and the local branch following it): an outlined
  `default` chip in `accent.primary`.
* **Upstream gone** (deleted on the remote): a ⚠ glyph, `semantic.warn` text and a `gone` chip.

Ctrl- or Shift-clicking a second branch compares the two.

---

## 1. `history` — commit graph

A header, the graph, then — below a splitter — the selected commit.

### Header

`surface.header`, one line: **All commits** (or **Matching commits**, with a note that lines are
not drawn between matches), then at the right an author list (`Any author`), a date button
(`Any time`), and the text filter (`Filter commits…`).

### Columns

No column header row — the columns are clear from their contents. Left to right: graph and ref
chips, message, author, date, short id. The graph column is sized to the lanes actually used.
When the window narrows, the id, date and author columns drop out, in that order, before the
message goes below its minimum.

### Rows

Height `metrics.graph.rowHeight`.

> **The 1px trap.** Lane geometry is computed from the row height. If a border adds to a row, the
> painted pitch drifts from the geometry by a pixel a row — 14px off by row 15, which reads as a
> broken graph. The row height and the painter's pitch come from one constant.

Selected row: `selection.bg`; message `text.selectedPrimary`, metadata `text.selectedDim`.
Unselected: message `text.default`, metadata `text.faint`.

### Ref chips

Inline before the subject, taking at most two fifths of the message column; any that do not fit
become **+N**. 10.5px monospace, radius 3, a 1px border.

| Ref | Text | Fill | Border |
|---|---|---|---|
| Current branch | `semantic.warn` | warn at 16% | warn at 50% |
| Local branch | `accent.primaryLight` | `fills.infoChip` | `fills.infoChipBorder` |
| Remote branch | `text.dim` | neutral at 12% | neutral at 34% |
| Tag | `semantic.meta` | `fills.metaChip` | `fills.metaChipBorder` |

### Lane painting

Painted under the row text, not per cell.

- lane *n* centre x = `firstLaneX + n × lanePitch` (18 + n × 14)
- row *i* centre y = `i × rowHeight + rowHeight / 2`
- lane colour = `graphLanes[lane % 11]`; the light themes use their own darker set, same hues
- an edge takes the **parent's** lane colour, 1.6px, round caps
- same-lane edge: a straight line; lane-changing edge: a cubic Bézier,
  `M x1 y1 C x1 (y1+13), x2 (y2−15), x2 y2`
- commit node: filled circle, r 3.6, 1.2px stroke in the lane colour
- **merge node: hollow** — `surface.window` fill, r 4.2, 1.8px stroke

A filtered history is a list, not a graph: matches sit on one lane, with no edges.

### The selected commit

A one-line summary — subject, then author · date · short id — then two tabs, **Commit** and
**Changes · N**. Changes is the one open by default.

* **Changes:** the changed files beside the diff of the selected one. An image shows the image
  comparison (§3) in the diff's place.
* **Commit:** the whole record — subject and message, author and committer with their dates, the
  full id, and the parents.

Selecting two branches in the sidebar shows the comparison here instead, with a Swap button.

### File diff

11.5px monospace, 18px lines. Two right-aligned line-number gutters (old, new), a sign column, then
the code.

- added: `fills.addRow`, gutter `fills.addGutter`, text `semantic.addText`, sign `+`
- removed: `fills.removeRow`, gutter `fills.removeGutter`, text `semantic.removeText`, sign `−`
- context: no fill, gutter `surface.gutter`
- hunk header: full width on `surface.hunkHeader`, text `text.hunkHeader`, e.g.
  `@@ -41,12 +41,24 @@ void Ship::update`

---

## 2. `working` — local changes and staging

A list column (396px by default), the diff filling the rest.

### Lists

Two sections, each with a header on `surface.header`: the title in `type.sectionLabel` and a text
action in `accent.primary` at the right.

- `WORKING COPY · 5 FILES` / `Stage all`
- `STAGED · 2 FILES` / `Unstage all`

### Rows — two lines each

Line 1: checkbox, status letter in a 12px monospace box, file name, state chip. Line 2: the
directory in 10.5px monospace `text.label`, and the states that apply.

The checkbox *is* the staged state: filled `accent.primary` with a ✓ in Staged, empty in the working
copy. Ticking it stages (or unstages) the file — the whole selection, when the row is part of it.
Right-clicking inside a selection acts on the selection: stage, stash, discard, copy paths.

Chips, one per row by priority — `conflict` (remove-tinted), then `locked` (warn), then `partial`
(neutral) — with every state that applies named on the second line. Rows in a warning state use
`semantic.warnSoft` for the second line.

### Commit box

Below the lists: the message field on `surface.sunken`, then **Amend** at the left and the primary
button at the right — `accent.buttonBg`, labelled `Commit 2 files`.

**Commit reason.** Whenever the button is disabled, a line under it says why — no message, nothing
staged, unresolved conflicts — in `semantic.removeSoft` for conflicts and `text.label` otherwise.
The button is never disabled without a reason on screen.

### Diff — the staging variant

The same diff as history, plus:

- a pick gutter at the left; changed lines show a `·` to click for line staging
- hunk headers carry `Stage hunk` (add-outlined) and `Discard` (remove-outlined) at the right, and
  the staging count: `@@ -12,6 +12,9 @@  ·  2 of 4 lines staged`
- a `Stage selected lines` button below the diff

---

## 3. `image` — image comparison

Shown in the diff's place for an image file. A mode control — **Side by side**, **Difference**,
**Onion skin** — above the images; each pane has a caption (which side, and its size and format)
over a checkerboard, so transparency is visible. When the file is in LFS and its object has not been
fetched, the pane says so rather than showing a broken image.

| Mode | Shows |
|---|---|
| Side by side | Both versions, each in its own pane |
| Difference | One pane: the difference blend, and the share of pixels that changed |
| Onion skin | One pane: the new version over the old, with adjustable opacity |

---

## Interactions

| Interaction | Result |
|---|---|
| Click a commit | Selects it; the Changes tab lists its files and shows the first one's diff |
| Click a branch or tag | Shows history, and selects its commit |
| Ctrl- or Shift-click a second branch | Compares the two |
| Double-click a branch | Checks it out, asking what to do with uncommitted changes |
| Click `Local Changes` / `All Commits` | Switches the main area |
| Click a pick-gutter dot | Stages or unstages that line; the row's chip becomes `partial` |
| `Stage hunk` / `Discard` | Stages or discards the hunk; discarding asks first, and can be undone |
| Tick a row's checkbox | Stages it (unstages it, in Staged) — the whole selection when the row is in it |
| Commit while disabled | Nothing — the reason is already on screen |
