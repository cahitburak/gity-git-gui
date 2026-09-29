# QT_MAPPING — how the UI is built in Qt Widgets

How each surface in [`SPEC.md`](./SPEC.md) is built, and the rules that keep it fast and themeable.
Qt 6 Widgets, not QML (ARCHITECTURE.md, ADR-002).

## Styling

**Nothing outside the theme layer writes a colour.** Three mechanisms, each for its own job:

- **Tokens.** `tools/generate-tokens.py` turns `TOKENS.json` into `src/ui/theme/Tokens.h`: `QColor`
  and `int` constants. Everything painted by hand — the graph, the diff, the list delegates, the
  toolbar icons — reads these, so it follows a theme change on the next paint. A theme is a set of
  overrides applied to them (`Theme.cpp`, and [`THEMES.md`](./THEMES.md)).
- **One stylesheet**, `src/ui/theme/gity.qss`, for the chrome: menus, toolbar, inputs, buttons,
  panel surfaces, headers. It names tokens between at-signs (`@surfaceWindow@`), substituted when the
  sheet is loaded; a name that does not exist is reported rather than silently becoming no colour.
  Widgets are styled by object name or by a `gityRole` property (`note`, `warn`, `error`, `primary`,
  `destructive`) — **never** with `setStyleSheet` on the widget, because a sheet set there freezes
  the colours of the moment and ignores the next theme change.
- **The palette** is filled from the tokens too, so any stock widget that is not styled still lands
  in the theme rather than the desktop's.

Icons are drawn by `icons::themed()` at paint time, in the current text colour, for the same reason:
a pixmap made once keeps the colour it was made with.

Fonts: the UI font comes from the platform; only sizes and weights come from the tokens. The
monospace font is resolved explicitly with `QFontDatabase::systemFont(QFontDatabase::FixedFont)`,
since diff alignment breaks on a proportional fallback.

High DPI: everything is painted in logical pixels and Qt scales. Lane geometry is integer-derived —
no floats accumulated per row (see the 1px trap in `SPEC.md`).

## Surfaces

### Window shell

`QMainWindow` with the native title bar, `QMenuBar`, a fixed `QToolBar`, and a `QStatusBar` whose
permanent widgets hold the command log button and the commit count. Repository tabs are a
`QTabBar` in their own band. Panes are named `QSplitter`s, non-collapsible with explicit floors;
their sizes are saved per splitter.

### Sidebar

`RefsSidebar`, a `QTreeWidget` — a refs tree has hundreds of rows at most, so the stock widget and
its keyboard and accessibility behaviour are the right tool. One column; `RefsSidebarDelegate`
paints the glyph, the elided label, the tag chip, the badge chip, and the 2px selection marker the
default selection rectangle cannot produce. Section headings are unselectable rows with their own
paint branch, not group boxes.

The tree is rebuilt on every refresh, so the selection and folded sections are restored by a stable
key per row, not by position.

### Commit graph

`CommitGraphView`, a `QAbstractScrollArea` that paints only the visible rows from `HistoryModel` —
not a `QTreeView`: at a million rows, the item-view machinery costs more than it gives.

- Lane data lives in the model: per row, the commit's lane and the edges passing through it,
  computed as the history streams in (ARCHITECTURE.md, ADR-005). The view only draws.
- Each row paints its own segment of every edge crossing it, so scrolling needs no overlay to keep
  in sync.
- The row height and the painter's lane pitch come from one constant.
- Antialiasing on for lanes, off for text.
- Ref chips are painted, not widgets; their widths are measured once per row, not every frame.

`GraphColumns` holds the column geometry in one place, so painting and hit-testing agree as the
window resizes.

### Diff views

`DiffView`, a `QAbstractScrollArea` that paints only the visible lines — never `QTextEdit`, which
cannot hold a 250,000-line file or hit-test a gutter.

- Hit-testing is arithmetic: y maps to a line, x to a gutter. That is what makes line staging
  cheap.
- Hunk headers are rows in the same line list; their `Stage hunk` and `Discard` buttons are painted
  and hit-tested, not `QPushButton`s — child widgets in a scrolling view are a performance trap.
- One view serves history, local changes (in staging mode) and the rebase dialog (read-only).

### Working copy lists

Two `QTreeWidget`s with `WorkingCopyDelegate`, a two-line row (`sizeHint` ≈ 46px) whose checkbox is
painted and hit-tested by an event filter on the viewport. A rename is one row but two paths to
git; staging or unstaging it passes both.

The commit button's enabled state comes from one function that returns a reason string, and the
line under the button shows that reason — one code path for both.

### Image comparison

`ImageDiffView`, a plain `QWidget`. Images are decoded on the session worker, never in
`paintEvent`; the checkerboard is a cached brush. Difference and onion skin are
`QPainter::CompositionMode_Difference` and an alpha blend over a cached composite, recomputed when
the images change, not on every repaint.

## Threading

Nothing that touches a repository runs on the UI thread. History streams in from the session
worker in batches, appended to the model as they arrive, so the first rows paint before the walk
finishes (ARCHITECTURE.md, ADR-004). Details, diffs and images are worker jobs too, and a result
superseded by a newer request is dropped rather than waited on.
