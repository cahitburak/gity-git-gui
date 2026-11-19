#include "SyntaxHighlight.h"

#include "ui/theme/Theme.h"

#include <QFontMetrics>
#include <QPainter>

#ifdef GITY_HAVE_SYNTAX_HIGHLIGHTING
#include <KSyntaxHighlighting/AbstractHighlighter>
#include <KSyntaxHighlighting/Definition>
#include <KSyntaxHighlighting/Format>
#include <KSyntaxHighlighting/Repository>
#include <KSyntaxHighlighting/State>
#include <KSyntaxHighlighting/Theme>
#endif

#include <algorithm>

namespace gity::ui {

namespace syntax {
namespace {

/// Past these a file is drawn plain: colouring it would hold up the window
/// for longer than the colours are worth.
constexpr std::size_t kMaxBytes = std::size_t{2} << 20;
constexpr std::size_t kMaxLines = 60000;
/// Past this a line is drawn plain, though the lines round it are coloured:
/// a minified line of a megabyte is regular expressions run over a megabyte,
/// on the window's thread, for colours nobody reads.
constexpr std::size_t kMaxLineLength = 8192;

#ifdef GITY_HAVE_SYNTAX_HIGHLIGHTING

namespace kh = KSyntaxHighlighting;

/// Loaded once: reading the definitions' index is the slow part.
kh::Repository& repository() {
    static kh::Repository instance;
    return instance;
}

bool lightTheme() {
    return Theme::isLight(Theme::effectiveVariant());
}

QString themeName() {
    return lightTheme() ? QStringLiteral("GitHub Light") : QStringLiteral("GitHub Dark");
}

kh::Theme currentTheme() {
    kh::Repository& repo = repository();
    kh::Theme theme = repo.theme(themeName());
    if (!theme.isValid()) {
        theme = repo.defaultTheme(lightTheme() ? kh::Repository::LightTheme
                                               : kh::Repository::DarkTheme);
    }
    return theme;
}

/// Collects what the highlighter says about one line at a time.
class Collector final : public kh::AbstractHighlighter {
public:
    LineSpans* out = nullptr;

    kh::State next(QStringView line, const kh::State& state) { return highlightLine(line, state); }

protected:
    void applyFormat(int offset, int length, const kh::Format& format) override {
        if (length <= 0 || out == nullptr || format.isDefaultTextStyle(theme())) {
            return;
        }
        CodeSpan span;
        span.start = offset;
        span.length = length;
        if (format.hasTextColor(theme())) {
            span.color = format.textColor(theme());
        }
        span.bold = format.isBold(theme());
        span.italic = format.isItalic(theme());
        if (!span.color.isValid() && !span.bold && !span.italic) {
            return;
        }
        // Next to the one before in the same style: one run, not two.
        if (!out->empty()) {
            CodeSpan& last = out->back();
            if (last.start + last.length == span.start && last.color == span.color &&
                last.bold == span.bold && last.italic == span.italic) {
                last.length += span.length;
                return;
            }
        }
        out->push_back(span);
    }
};

/// Runs `next` until it returns false, colouring each line it gives. A line
/// it says is too long gets no colours and goes to the highlighter as an
/// empty one, so what ends with a line — a "//" comment, say — still ends
/// there.
template <typename Next> FileSpansPtr run(const QString& path, Next&& next) {
    const kh::Definition definition = repository().definitionForFileName(path);
    if (!definition.isValid()) {
        return nullptr;
    }
    Collector collector;
    collector.setDefinition(definition);
    collector.setTheme(currentTheme());
    auto spans = std::make_shared<FileSpans>();
    kh::State state;
    QString line;
    bool tooLong = false;
    while (next(line, tooLong)) {
        spans->emplace_back();
        if (tooLong) {
            collector.out = nullptr;
            state = collector.next(QStringView(), state);
            continue;
        }
        collector.out = &spans->back();
        state = collector.next(line, state);
    }
    return spans;
}

#endif

} // namespace

bool available() {
#ifdef GITY_HAVE_SYNTAX_HIGHLIGHTING
    return true;
#else
    return false;
#endif
}

QString themeKey() {
#ifdef GITY_HAVE_SYNTAX_HIGHLIGHTING
    return themeName();
#else
    return {};
#endif
}

FileSpansPtr highlight(const QString& path, std::string_view text, std::size_t lines) {
#ifdef GITY_HAVE_SYNTAX_HIGHLIGHTING
    if (text.size() > kMaxBytes) {
        return nullptr;
    }
    const std::size_t limit = lines == 0 ? kMaxLines : std::min(lines, kMaxLines);
    std::size_t at = 0;
    std::size_t done = 0;
    return run(path, [&](QString& line, bool& tooLong) {
        if (at >= text.size() || done >= limit) {
            return false;
        }
        std::size_t end = text.find('\n', at);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        std::string_view piece = text.substr(at, end - at);
        if (!piece.empty() && piece.back() == '\r') {
            piece.remove_suffix(1);
        }
        tooLong = piece.size() > kMaxLineLength;
        if (tooLong) {
            line.clear();
        } else {
            line = QString::fromUtf8(piece.data(), static_cast<qsizetype>(piece.size()));
        }
        at = end + 1;
        ++done;
        return true;
    });
#else
    (void)path;
    (void)text;
    (void)lines;
    return nullptr;
#endif
}

FileSpansPtr highlightLines(const QString& path, const std::vector<QString>& lines) {
#ifdef GITY_HAVE_SYNTAX_HIGHLIGHTING
    if (lines.size() > kMaxLines) {
        return nullptr;
    }
    // The same budget as a file's bytes, counted in characters, of which
    // there are no more than bytes: this lets through no more than
    // highlight() does.
    std::size_t total = 0;
    for (const QString& line : lines) {
        total += static_cast<std::size_t>(line.size());
        if (total > kMaxBytes) {
            return nullptr;
        }
    }
    std::size_t at = 0;
    return run(path, [&](QString& line, bool& tooLong) {
        if (at >= lines.size()) {
            return false;
        }
        const QString& next = lines[at++];
        tooLong = static_cast<std::size_t>(next.size()) > kMaxLineLength;
        if (tooLong) {
            line.clear();
        } else {
            line = next;
        }
        return true;
    });
#else
    (void)path;
    (void)lines;
    return nullptr;
#endif
}

} // namespace syntax

namespace {

/// `text` with its tabs as spaces, counting columns on from `column`.
QString expandTabs(QStringView text, int& column) {
    QString out;
    out.reserve(text.size());
    for (const QChar c : text) {
        if (c == QLatin1Char('\t')) {
            const int room = kCodeTabWidth - (column % kCodeTabWidth);
            out += QString(room, QLatin1Char(' '));
            column += room;
        } else {
            out += c;
            ++column;
        }
    }
    return out;
}

} // namespace

int codeLineColumns(QStringView text) {
    int column = 0;
    for (const QChar c : text) {
        column += c == QLatin1Char('\t') ? kCodeTabWidth - (column % kCodeTabWidth) : 1;
    }
    return column;
}

int codeLineColumns(std::string_view utf8) {
    int column = 0;
    for (const char c : utf8) {
        if (c == '\t') {
            column += kCodeTabWidth - (column % kCodeTabWidth);
        } else if (c != '\n' && c != '\r' && (static_cast<unsigned char>(c) & 0xC0) != 0x80) {
            // A continuation byte is part of the character before it.
            ++column;
        }
    }
    return column;
}

void drawCodeLine(QPainter& painter, int x, int y, int height, QStringView text,
                  const LineSpans* spans, const QColor& plain, int spanShift, int startColumn) {
    const QFont base = painter.font();
    const QFontMetrics metrics(base);
    // Where QPainter puts a line centred in a rectangle, so a row coloured
    // and a row drawn plain sit on the same baseline.
    const int baseline = y + (height - metrics.height()) / 2 + metrics.ascent();
    int column = startColumn;
    int left = x;
    const qsizetype size = text.size();

    const auto draw = [&](qsizetype from, qsizetype to, const CodeSpan* span) {
        if (to <= from) {
            return;
        }
        const QString shown = expandTabs(text.mid(from, to - from), column);
        QColor colour = plain;
        if (span != nullptr && span->color.isValid()) {
            colour = span->color;
        }
        painter.setPen(colour);
        if (span != nullptr && (span->bold || span->italic)) {
            QFont styled = base;
            styled.setBold(span->bold);
            styled.setItalic(span->italic);
            painter.setFont(styled);
            painter.drawText(left, baseline, shown);
            painter.setFont(base);
        } else {
            painter.drawText(left, baseline, shown);
        }
        // Measured in the plain font: bold must not push the rest of the
        // line off the grid every other line keeps.
        left += metrics.horizontalAdvance(shown);
    };

    qsizetype at = 0;
    if (spans != nullptr) {
        for (const CodeSpan& span : *spans) {
            const qsizetype start = std::max<qsizetype>(span.start - spanShift, 0);
            const qsizetype end = std::min<qsizetype>(span.start + span.length - spanShift, size);
            if (end <= at) {
                continue;
            }
            if (start >= size) {
                break;
            }
            draw(at, std::max(start, at), nullptr);
            draw(std::max(start, at), end, &span);
            at = end;
        }
    }
    draw(at, size, nullptr);
}

} // namespace gity::ui
