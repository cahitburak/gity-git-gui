// Colours for code: which parts of a line are keywords, strings, comments.
//
// KSyntaxHighlighting does the reading — some three hundred languages, chosen
// by file name — when the build found it; without it every function here
// answers "nothing to colour" and code is drawn plain, as before. The views
// stay the same either way.
//
// A file is highlighted whole, from its first line: whether line 400 is
// inside a comment depends on the lines above it. The views ask once per file
// shown and keep the answer, keyed by themeKey() so a change of theme colours
// it again.
#pragma once

#include <QColor>
#include <QString>
#include <QStringView>

#include <memory>
#include <string_view>
#include <vector>

class QPainter;

namespace gity::ui {

struct CodeSpan {
    int start = 0; ///< In UTF-16 units of the line.
    int length = 0;
    QColor color; ///< Invalid: the view's own text colour.
    bool bold = false;
    bool italic = false;
};

using LineSpans = std::vector<CodeSpan>;
/// One file's colours: the spans of each line, from line 1.
using FileSpans = std::vector<LineSpans>;
using FileSpansPtr = std::shared_ptr<const FileSpans>;

namespace syntax {

/// This build can highlight at all.
[[nodiscard]] bool available();

/// Different whenever the same file would be coloured differently — the
/// theme changed between light and dark. Empty when nothing is coloured.
[[nodiscard]] QString themeKey();

/// `text` as the file `path` holds it: lines split at '\n', a '\r' before one
/// dropped. Only the first `lines` are coloured, 0 meaning all — what comes
/// after the last line shown cannot change it. Null when no language matches
/// the name, the file is too large to be worth it, or the build has none.
[[nodiscard]] FileSpansPtr highlight(const QString& path, std::string_view text,
                                     std::size_t lines = 0);

/// The same for a file already in lines, without their endings.
[[nodiscard]] FileSpansPtr highlightLines(const QString& path, const std::vector<QString>& lines);

/// The spans of line `number`, from 1; null past the end or without spans.
[[nodiscard]] inline const LineSpans* lineSpans(const FileSpansPtr& spans, std::size_t number) {
    if (!spans || number == 0 || number > spans->size()) {
        return nullptr;
    }
    return &(*spans)[number - 1];
}

} // namespace syntax

/// How many columns a tab takes in every code view.
inline constexpr int kCodeTabWidth = 4;

/// Draws a line of code with its left edge at `x`, centred in the row `y`,
/// `height` high, in the painter's font. Tabs go to the next multiple of
/// kCodeTabWidth, counting from `startColumn` — where `text` starts in its
/// line when the view skipped what was scrolled off. `spans`, when given, are
/// offsets into the whole line, so `spanShift` — how many characters were
/// skipped — is taken off them. Elsewhere the text is `plain`.
void drawCodeLine(QPainter& painter, int x, int y, int height, QStringView text,
                  const LineSpans* spans, const QColor& plain, int spanShift = 0,
                  int startColumn = 0);

/// How wide drawCodeLine draws `text`, in the painter's font metrics given.
[[nodiscard]] int codeLineColumns(QStringView text);

} // namespace gity::ui
