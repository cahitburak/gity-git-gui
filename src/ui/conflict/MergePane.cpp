#include "MergePane.h"

#include "ui/theme/Theme.h"
#include "ui/theme/Tokens.h"

#include <QContextMenuEvent>
#include <QFontDatabase>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QScrollBar>

#include <algorithm>

namespace gity::ui {
namespace {

using Column = ConflictDocument::Column;
using Kind = git::MergeRegion::Kind;
using git::Pick;

/// A line as painted: its ending gone. Tabs are drawCodeLine's.
QString displayed(const std::string& line) {
    std::string_view view(line);
    while (!view.empty() && (view.back() == '\n' || view.back() == '\r')) {
        view.remove_suffix(1);
    }
    return QString::fromUtf8(view.data(), static_cast<qsizetype>(view.size()));
}

bool takesOurs(Pick pick) {
    return pick == Pick::Ours || pick == Pick::OursThenTheirs || pick == Pick::TheirsThenOurs;
}

bool takesTheirs(Pick pick) {
    return pick == Pick::Theirs || pick == Pick::OursThenTheirs || pick == Pick::TheirsThenOurs;
}

} // namespace

MergePane::MergePane(ConflictDocument* document, ConflictDocument::Column column, QWidget* parent)
    : QAbstractScrollArea(parent), document_(document), column_(column) {
    setFrameShape(QFrame::NoFrame);
    viewport()->setMouseTracking(true);
    connect(document_, &ConflictDocument::changed, this, [this] {
        // Ours and theirs change only with the file; the result with every
        // choice made.
        if (column_ == Column::Result || document_->file() != spansFile_.lock()) {
            spansReady_ = false;
        }
        longestLine_ = 0;
        for (std::size_t i = 0; i < document_->regionCount(); ++i) {
            for (const auto& line : document_->lines(column_, i)) {
                // Counted on the bytes: this runs over every line of every
                // column at each change.
                longestLine_ = std::max(longestLine_, codeLineColumns(std::string_view(line)));
            }
        }
        updateScrollBars();
        viewport()->update();
    });
}

QFont MergePane::monoFont() const {
    if (!monoFontValid_) {
        monoFont_ = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        monoFont_.setPointSizeF(QFontInfo(font()).pointSizeF());
        monoFontValid_ = true;
    }
    return monoFont_;
}

void MergePane::highlight() {
    const QString key = syntax::themeKey();
    if (spansReady_ && key == spansKey_) {
        return;
    }
    spansReady_ = true;
    spansKey_ = key;
    spansFile_ = document_->file();
    spans_.reset();
    if (key.isEmpty() || !document_->file()) {
        return;
    }
    // The column's whole file, region after region: for ours and theirs the
    // versions git kept, for the result what it holds so far.
    std::string text;
    for (std::size_t i = 0; i < document_->regionCount(); ++i) {
        for (const auto& line : document_->lines(column_, i)) {
            text += line;
        }
    }
    spans_ = syntax::highlight(QString::fromStdString(document_->file()->path), text);
}

int MergePane::rowHeight() const {
    return QFontMetrics(monoFont()).height() + 3;
}

int MergePane::charWidth() const {
    return std::max(1, QFontMetrics(monoFont()).horizontalAdvance(QLatin1Char('0')));
}

int MergePane::gutterWidth() const {
    const int digits = static_cast<int>(QString::number(document_->longestFile()).size());
    return charWidth() * (std::max(digits, 3) + 2) + 4;
}

void MergePane::changeEvent(QEvent* event) {
    if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange) {
        monoFontValid_ = false;
        updateScrollBars();
    }
    QAbstractScrollArea::changeEvent(event);
}

int MergePane::verticalReach() const {
    // A little room under the last line, so the end of the file does not sit
    // flush against the bottom edge.
    return std::max(0, (document_->rowCount() + 2) * rowHeight() - viewport()->height());
}

void MergePane::setSharedReach(int reach) {
    if (reach == sharedReach_) {
        return;
    }
    sharedReach_ = reach;
    updateScrollBars();
}

void MergePane::updateScrollBars() {
    const int h = rowHeight();
    const int page = viewport()->height();
    // As far as its own height needs, or as the furthest pane beside it does:
    // a pane with a scroll bar under it, or the result below the others, is
    // shorter, and must not stop short of where the others scroll to.
    const int reach = verticalReach();
    verticalScrollBar()->setRange(0, std::max(reach, sharedReach_));
    verticalScrollBar()->setPageStep(page);
    verticalScrollBar()->setSingleStep(h);
    const int textRoom = viewport()->width() - gutterWidth();
    horizontalScrollBar()->setRange(0, std::max(0, (longestLine_ + 4) * charWidth() - textRoom));
    horizontalScrollBar()->setPageStep(std::max(1, textRoom));
    horizontalScrollBar()->setSingleStep(charWidth() * 4);
    placeEditor();
    emit reachChanged();
}

void MergePane::resizeEvent(QResizeEvent* event) {
    QAbstractScrollArea::resizeEvent(event);
    updateScrollBars();
}

void MergePane::scrollContentsBy(int dx, int dy) {
    QAbstractScrollArea::scrollContentsBy(dx, dy);
    placeEditor();
    viewport()->update();
}

void MergePane::setCurrentRegion(std::optional<std::size_t> region) {
    current_ = region;
    viewport()->update();
}

std::optional<std::size_t> MergePane::regionAt(int viewportY) const {
    if (document_->rowCount() == 0) {
        return std::nullopt;
    }
    const int row = (viewportY + verticalScrollBar()->value()) / rowHeight();
    if (row < 0 || row >= document_->rowCount()) {
        return std::nullopt;
    }
    return document_->placeAt(row).region;
}

QColor MergePane::bandFor(std::size_t index) const {
    const git::MergeRegion& region = document_->region(index);
    const Pick pick = document_->pick(index);
    switch (column_) {
    case Column::Ours:
        if (region.kind == Kind::Ours || region.kind == Kind::Conflict) {
            return tokens::fillInfoChip;
        }
        return region.kind == Kind::Both ? tokens::fillNeutralChip : QColor();
    case Column::Theirs:
        if (region.kind == Kind::Theirs || region.kind == Kind::Conflict) {
            return tokens::fillMetaChip;
        }
        return region.kind == Kind::Both ? tokens::fillNeutralChip : QColor();
    case Column::Result:
        break;
    }
    if (region.kind == Kind::Same && pick == Pick::Ours) {
        return {};
    }
    switch (pick) {
    case Pick::Unresolved:
        return tokens::fillWarnChip;
    case Pick::Ours:
        return region.kind == Kind::Both ? tokens::fillNeutralChip : tokens::fillInfoChip;
    case Pick::Theirs:
        return tokens::fillMetaChip;
    case Pick::OursThenTheirs:
    case Pick::TheirsThenOurs:
    case Pick::Base:
    case Pick::Edited:
        break;
    }
    return tokens::fillNeutralChip;
}

bool MergePane::dimmed(std::size_t index) const {
    // A side's lines that are not in the result read as set aside.
    const git::MergeRegion& region = document_->region(index);
    const Pick pick = document_->pick(index);
    if (region.kind == Kind::Same || pick == Pick::Unresolved) {
        return false;
    }
    if (column_ == Column::Ours) {
        return region.kind != Kind::Theirs && !takesOurs(pick) && pick != Pick::Base;
    }
    if (column_ == Column::Theirs) {
        return region.kind != Kind::Ours && !takesTheirs(pick) && pick != Pick::Base;
    }
    return false;
}

std::vector<MergePane::Chip> MergePane::chipsFor(std::size_t index, int y, int left) const {
    std::vector<Chip> chips;
    if (document_->region(index).kind != Kind::Conflict) {
        return chips;
    }
    const Pick pick = document_->pick(index);

    struct Want {
        Action action;
        Pick pick;
    };

    std::vector<Want> wanted;
    switch (column_) {
    case Column::Ours:
        wanted.push_back({Action::Pick, Pick::Ours});
        break;
    case Column::Theirs:
        wanted.push_back({Action::Pick, Pick::Theirs});
        break;
    case Column::Result:
        wanted = {{Action::Pick, Pick::Ours},
                  {Action::Pick, Pick::Theirs},
                  {Action::Pick, Pick::OursThenTheirs},
                  {Action::Edit, Pick::Edited}};
        break;
    }
    const QFontMetrics fm(font());
    const int h = rowHeight() - 4;
    int x = left;
    for (auto it = wanted.begin(); it != wanted.end(); ++it) {
        Chip chip;
        chip.region = index;
        chip.action = it->action;
        chip.pick = it->pick;
        chip.taken = chip.action == Action::Pick && pick == chip.pick;
        const bool ours = chip.pick == Pick::Ours;
        if (chip.action == Action::Edit) {
            chip.label = tr("Edit");
        } else if (chip.taken) {
            chip.label = QStringLiteral("✓ ") + (ours                        ? tr("Ours")
                                                 : chip.pick == Pick::Theirs ? tr("Theirs")
                                                                             : tr("Both"));
        } else if (chip.pick == Pick::OursThenTheirs) {
            chip.label = tr("Both");
        } else if (column_ == Column::Result) {
            chip.label = ours ? tr("Ours") : tr("Theirs");
        } else {
            chip.label = ours ? tr("Take Ours") : tr("Take Theirs");
        }
        const int w = fm.horizontalAdvance(chip.label) + 16;
        chip.rect = QRect(x, y + 2, w, h);
        chips.push_back(chip);
        x += w + 4;
    }
    return chips;
}

void MergePane::paintEvent(QPaintEvent* event) {
    const Theme& theme = Theme::current();
    QPainter painter(viewport());
    painter.fillRect(event->rect(), theme.background);
    if (document_->rowCount() == 0) {
        return;
    }

    const QFont mono = monoFont();
    const QFontMetrics fm(mono);
    const int h = rowHeight();
    const int scroll = verticalScrollBar()->value();
    const int gutter = gutterWidth();
    const int width = viewport()->width();
    const int textX = gutter + 6 - horizontalScrollBar()->value();
    const int first = std::max(0, (scroll + event->rect().top()) / h);
    const int last = std::min(document_->rowCount() - 1, (scroll + event->rect().bottom()) / h);

    painter.fillRect(0, 0, gutter, viewport()->height(), theme.gutterBg);
    highlight();

    for (int row = first; row <= last; ++row) {
        const ConflictDocument::Place place = document_->placeAt(row);
        const std::size_t index = place.region;
        const git::MergeRegion& region = document_->region(index);
        const git::Lines& lines = document_->lines(column_, index);
        const int y = row * h - scroll;
        const bool changed = region.kind != Kind::Same;
        const int header = document_->headerRows(index);
        const int lineIndex = place.offset - header;
        const bool isHeader = lineIndex < 0;
        const bool filler = !isHeader && lineIndex >= static_cast<int>(lines.size());

        if (const QColor band = bandFor(index); band.isValid()) {
            painter.fillRect(gutter, y, width - gutter, h, band);
        }
        if (isHeader) {
            // A second wash: the header reads as the conflict's title bar.
            painter.fillRect(gutter, y, width - gutter, h, tokens::fillNeutralChip);
        } else if (filler && changed) {
            // No line of this column here: the rows exist only to keep the
            // columns beside it level.
            painter.fillRect(gutter, y, width - gutter, h,
                             QBrush(theme.separator, Qt::BDiagPattern));
        }
        if (changed && place.offset == 0) {
            painter.setPen(theme.separator);
            painter.drawLine(0, y, width, y);
        }
        if (changed && place.offset == document_->regionRows(index) - 1) {
            painter.setPen(theme.separator);
            painter.drawLine(0, y + h - 1, width, y + h - 1);
        }
        if (current_ && *current_ == index) {
            painter.fillRect(gutter - 3, y, 3, h, tokens::accentPrimary);
        }

        if (isHeader) {
            paintHeader(painter, index, y);
            continue;
        }
        if (filler) {
            continue;
        }
        painter.setFont(mono);
        painter.setPen(theme.gutterText);
        painter.drawText(QRect(0, y, gutter - 10, h), Qt::AlignRight | Qt::AlignVCenter,
                         QString::number(document_->firstLine(column_, index) + lineIndex));
        painter.save();
        painter.setClipRect(gutter, y, width - gutter, h);
        const bool faint = dimmed(index);
        drawCodeLine(painter, textX, y, h, displayed(lines[static_cast<std::size_t>(lineIndex)]),
                     faint ? nullptr
                           : syntax::lineSpans(
                                 spans_, static_cast<std::size_t>(
                                             document_->firstLine(column_, index) + lineIndex)),
                     faint ? theme.textFaint : theme.text);
        painter.restore();
    }

    painter.setPen(theme.separator);
    painter.drawLine(gutter - 1, 0, gutter - 1, viewport()->height());
}

MergePane::Header MergePane::header(std::size_t index, int y) const {
    const int gutter = gutterWidth();
    const QFontMetrics fm(font());
    Header made;

    // Which conflict, and in the result, what it holds now.
    QString& label = made.label;
    label = tr("Conflict %1 of %2")
                .arg(document_->conflictNumber(index))
                .arg(document_->conflicts().size());
    made.tone = Theme::current().textMuted;
    if (column_ == Column::Result) {
        switch (document_->pick(index)) {
        case Pick::Unresolved:
            label += tr(" · unresolved");
            made.tone = tokens::semanticWarn;
            break;
        case Pick::Ours:
            label += tr(" · ours");
            break;
        case Pick::Theirs:
            label += tr(" · theirs");
            break;
        case Pick::OursThenTheirs:
            label += tr(" · both, ours first");
            break;
        case Pick::TheirsThenOurs:
            label += tr(" · both, theirs first");
            break;
        case Pick::Base:
            label += tr(" · neither");
            break;
        case Pick::Edited:
            label += tr(" · edited");
            break;
        }
    }
    // Where the buttons would not fit beside the whole label, the number
    // alone says which conflict this is; the buttons matter more.
    int buttons = 0;
    for (const Chip& chip : chipsFor(index, y, 0)) {
        buttons = chip.rect.right();
    }
    if (gutter + 8 + fm.horizontalAdvance(label) + 12 + buttons > viewport()->width() - 8) {
        label = QStringLiteral("#%1").arg(document_->conflictNumber(index));
    }
    made.chips = chipsFor(index, y, gutter + 8 + fm.horizontalAdvance(label) + 12);
    return made;
}

void MergePane::paintHeader(QPainter& painter, std::size_t index, int y) {
    const Theme& theme = Theme::current();
    const int h = rowHeight();
    const int gutter = gutterWidth();
    const QFontMetrics fm(font());
    painter.setFont(font());

    const Header shown = header(index, y);
    painter.setPen(shown.tone);
    painter.drawText(QRect(gutter + 8, y, fm.horizontalAdvance(shown.label) + 4, h),
                     Qt::AlignLeft | Qt::AlignVCenter, shown.label);

    for (const Chip& chip : shown.chips) {
        const bool ours = chip.pick == Pick::Ours;
        const bool theirs = chip.pick == Pick::Theirs;
        const QColor fill = chip.taken ? (ours     ? tokens::fillInfoChip
                                          : theirs ? tokens::fillMetaChip
                                                   : tokens::fillNeutralChip)
                                       : tokens::surfaceHeader;
        const QColor border = ours     ? tokens::fillInfoChipBorder
                              : theirs ? tokens::fillMetaChipBorder
                                       : tokens::fillNeutralChipBorder;
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(border);
        painter.setBrush(fill);
        painter.drawRoundedRect(QRectF(chip.rect).adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);
        painter.setRenderHint(QPainter::Antialiasing, false);
        painter.setBrush(Qt::NoBrush);
        painter.setPen(ours ? tokens::semanticInfo : theirs ? tokens::semanticMeta : theme.text);
        painter.drawText(chip.rect, Qt::AlignCenter, chip.label);
    }
}

std::optional<MergePane::Chip> MergePane::chipAt(const QPoint& point) const {
    const int h = rowHeight();
    const int scroll = verticalScrollBar()->value();
    const int row = (point.y() + scroll) / h;
    if (point.y() < 0 || row >= document_->rowCount()) {
        return std::nullopt;
    }
    // Only a conflict's first row, its header, carries chips.
    const ConflictDocument::Place place = document_->placeAt(row);
    if (place.offset != 0 || document_->headerRows(place.region) == 0) {
        return std::nullopt;
    }
    for (const Chip& chip : header(place.region, row * h - scroll).chips) {
        if (chip.rect.contains(point)) {
            return chip;
        }
    }
    return std::nullopt;
}

void MergePane::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        QAbstractScrollArea::mousePressEvent(event);
        return;
    }
    const QPoint point = event->position().toPoint();
    if (const auto chip = chipAt(point); chip) {
        const Chip chosen = *chip;
        emit regionActivated(chosen.region);
        if (chosen.action == Action::Edit) {
            if (column_ == Column::Result) {
                editRegion(chosen.region);
            } else {
                emit editRequested(chosen.region);
            }
        } else {
            // Taking the side already taken a second time sets it aside.
            const bool again = document_->pick(chosen.region) == chosen.pick;
            document_->setPick(chosen.region, again ? Pick::Unresolved : chosen.pick);
        }
        return;
    }
    if (const auto region = regionAt(point.y()); region) {
        emit regionActivated(*region);
    }
}

void MergePane::mouseMoveEvent(QMouseEvent* event) {
    viewport()->setCursor(chipAt(event->position().toPoint()) ? Qt::PointingHandCursor
                                                              : Qt::ArrowCursor);
    QAbstractScrollArea::mouseMoveEvent(event);
}

void MergePane::mouseDoubleClickEvent(QMouseEvent* event) {
    const QPoint point = event->position().toPoint();
    if (column_ != Column::Result || chipAt(point)) {
        return;
    }
    if (const auto region = regionAt(point.y()); region) {
        emit regionActivated(*region);
        editRegion(*region);
    }
}

void MergePane::contextMenuEvent(QContextMenuEvent* event) {
    const auto region = regionAt(viewport()->mapFrom(this, event->pos()).y());
    if (!region) {
        return;
    }
    const std::size_t index = *region;
    const Kind kind = document_->region(index).kind;
    emit regionActivated(index);
    // The menu runs its own event loop: by the time a choice is made another
    // file may be shown, or this one read again after Start Over. A choice
    // acts only on the file it was offered for.
    const session::ConflictPtr file = document_->file();
    const auto stillShown = [this, file, index] {
        return document_->file() == file && index < document_->regionCount();
    };
    QMenu menu(this);
    const auto add = [&](const QString& text, Pick pick) {
        QAction* action = menu.addAction(text);
        action->setCheckable(true);
        action->setChecked(document_->pick(index) == pick);
        connect(action, &QAction::triggered, this, [this, index, pick, stillShown] {
            if (stillShown()) {
                document_->setPick(index, pick);
            }
        });
        return action;
    };
    if (kind != Kind::Same) {
        add(tr("Take Ours"), Pick::Ours);
        add(tr("Take Theirs"), Pick::Theirs);
        add(tr("Take Both, Ours First"), Pick::OursThenTheirs);
        add(tr("Take Both, Theirs First"), Pick::TheirsThenOurs);
        add(tr("Take Neither (As It Was Before)"), Pick::Base);
        menu.addSeparator();
    }
    QAction* edit = menu.addAction(tr("Edit Result…"));
    connect(edit, &QAction::triggered, this, [this, index, stillShown] {
        if (!stillShown()) {
            return;
        }
        if (column_ == Column::Result) {
            editRegion(index);
        } else {
            emit editRequested(index);
        }
    });
    if (kind == Kind::Conflict) {
        QAction* unresolve = menu.addAction(tr("Mark Unresolved"));
        unresolve->setEnabled(document_->pick(index) != Pick::Unresolved);
        connect(unresolve, &QAction::triggered, this, [this, index, stillShown] {
            if (stillShown()) {
                document_->setPick(index, Pick::Unresolved);
            }
        });
    }
    menu.exec(event->globalPos());
}

void MergePane::editRegion(std::size_t region) {
    if (column_ != Column::Result || region >= document_->regionCount()) {
        return;
    }
    finishEditing();
    editing_ = region;
    QStringList typed;
    const git::Lines& lines = document_->lines(Column::Result, region);
    const git::Lines& start = lines.empty() && document_->pick(region) == Pick::Unresolved
                                  ? document_->region(region).ours
                                  : lines;
    for (const auto& line : start) {
        std::string_view view(line);
        while (!view.empty() && (view.back() == '\n' || view.back() == '\r')) {
            view.remove_suffix(1);
        }
        typed << QString::fromUtf8(view.data(), static_cast<qsizetype>(view.size()));
    }
    auto* editor = new QPlainTextEdit(viewport());
    editor->setObjectName(QStringLiteral("conflictInlineEditor"));
    editor->setFont(monoFont());
    editor->setLineWrapMode(QPlainTextEdit::NoWrap);
    editor->setTabStopDistance(charWidth() * kCodeTabWidth);
    editStart_ = typed.join(QLatin1Char('\n'));
    editor->setPlainText(editStart_);
    editor->setToolTip(tr("Ctrl+Enter or click away to keep, Esc to cancel"));
    editor->installEventFilter(this);
    connect(editor, &QPlainTextEdit::textChanged, this, &MergePane::editChanged);
    editor_ = editor;
    // Where it stands, before it is shown, so it never flashes elsewhere.
    const int top = document_->regionRow(region) * rowHeight();
    if (top < verticalScrollBar()->value() ||
        top > verticalScrollBar()->value() + viewport()->height() - rowHeight() * 3) {
        verticalScrollBar()->setValue(std::max(0, top - rowHeight() * 2));
    }
    placeEditor();
    editor->show();
    editor->setFocus();
}

void MergePane::placeEditor() {
    if (!editor_) {
        return;
    }
    const int h = rowHeight();
    const int header = document_->headerRows(editing_);
    const int rows = std::max(document_->regionRows(editing_) - header, 3);
    const int y = (document_->regionRow(editing_) + header) * h - verticalScrollBar()->value();
    const int x = gutterWidth();
    editor_->setGeometry(x, y, std::max(80, viewport()->width() - x), rows * h + 8);
}

void MergePane::finishEditing() {
    stopEditing(true);
}

bool MergePane::editPending() const {
    return editor_ && editor_->toPlainText() != editStart_;
}

void MergePane::stopEditing(bool keep) {
    if (!editor_) {
        return;
    }
    QPlainTextEdit* editor = editor_;
    editor_ = nullptr; // first: the focus-out below comes back here
    const QString text = editor->toPlainText();
    // Where the keyboard was: closed from inside — Esc, Ctrl+Enter, a
    // shortcut — the pane keeps it; closed by a click on another widget,
    // that widget has it and keeps it.
    const bool hadFocus = editor->hasFocus();
    editor->disconnect(this);
    editor->removeEventFilter(this);
    editor->hide();
    editor->deleteLater();
    // Only what was changed: opening a conflict to look at ours' lines and
    // clicking away must not resolve it with them.
    if (keep && text != editStart_) {
        document_->setEdited(editing_, text);
    }
    if (hadFocus) {
        viewport()->setFocus();
    }
    emit editChanged();
}

bool MergePane::eventFilter(QObject* watched, QEvent* event) {
    if (watched == editor_) {
        if (event->type() == QEvent::KeyPress) {
            auto* key = static_cast<QKeyEvent*>(event);
            if (key->key() == Qt::Key_Escape) {
                stopEditing(false);
                return true;
            }
            if ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) &&
                (key->modifiers() & Qt::ControlModifier) != 0) {
                stopEditing(true);
                return true;
            }
        } else if (event->type() == QEvent::FocusOut) {
            // Not for the editor's own context menu, nor for another window
            // brought forward: the edit is still under way when they go.
            const Qt::FocusReason reason = static_cast<QFocusEvent*>(event)->reason();
            if (reason != Qt::PopupFocusReason && reason != Qt::ActiveWindowFocusReason) {
                stopEditing(true);
            }
        }
    }
    return QAbstractScrollArea::eventFilter(watched, event);
}

} // namespace gity::ui
