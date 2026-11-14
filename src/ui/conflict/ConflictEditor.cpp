#include "ConflictEditor.h"

#include "MergePane.h"
#include "ui/panels/Confirm.h"
#include "ui/theme/Tokens.h"

#include <QButtonGroup>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollBar>
#include <QShortcut>
#include <QSplitter>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

namespace gity::ui {
namespace {

using Column = ConflictDocument::Column;
using git::Pick;

QString fromStd(const std::string& text) {
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

QString commitLine(const git::ConflictCommit& commit) {
    if (commit.shortId.empty()) {
        return {};
    }
    return QStringLiteral("%1  %2").arg(fromStd(commit.shortId), fromStd(commit.summary));
}

} // namespace

ConflictEditor::ConflictEditor(QWidget* parent) : QWidget(parent) {
    qRegisterMetaType<Side>("gity::ui::ConflictEditor::Side");
    document_ = new ConflictDocument(this);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // --- header: the file, how far along, and the layout -----------------
    auto* header = new QWidget(this);
    header->setObjectName(QStringLiteral("conflictHeader"));
    header->setAttribute(Qt::WA_StyledBackground, true);
    auto* headerLayout = new QVBoxLayout(header);
    headerLayout->setContentsMargins(12, 8, 12, 8);
    headerLayout->setSpacing(6);

    auto* top = new QHBoxLayout;
    top->setSpacing(10);
    path_ = new QLabel(header);
    path_->setObjectName(QStringLiteral("conflictPath"));
    path_->setTextFormat(Qt::PlainText);
    status_ = new QLabel(header);
    status_->setTextFormat(Qt::PlainText);
    applyRole(status_, QStringLiteral("muted"));
    top->addWidget(path_);
    top->addWidget(status_);
    top->addStretch(1);

    auto* layoutLabel = new QLabel(tr("Layout"), header);
    applyRole(layoutLabel, QStringLiteral("muted"));
    top->addWidget(layoutLabel);
    auto* group = new QButtonGroup(this);
    group->setExclusive(true);
    const QString names[2] = {tr("Three Columns"), tr("Two Columns")};
    const QString tips[2] = {tr("Ours on the left, theirs on the right, the result between them "
                                "(Ctrl+1)"),
                             tr("Ours on the left, theirs on the right, the result in a row "
                                "below them (Ctrl+2)")};
    for (int i = 0; i < 2; ++i) {
        auto* button = new QToolButton(header);
        button->setObjectName(QStringLiteral("conflictLayout"));
        button->setText(names[i]);
        button->setToolTip(tips[i]);
        button->setCheckable(true);
        group->addButton(button, i);
        top->addWidget(button);
        layoutButtons_[i] = button;
        auto* shortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key(Qt::Key_1 + i)), this);
        shortcut->setContext(Qt::WidgetWithChildrenShortcut);
        connect(shortcut, &QShortcut::activated, button, &QToolButton::click);
    }
    layoutButtons_[0]->setChecked(true);
    connect(group, &QButtonGroup::idClicked, this, [this](int id) {
        setLayoutChoice(static_cast<Layout>(id));
        emit layoutChosen(layout_);
    });
    headerLayout->addLayout(top);

    toolbar_ = new QWidget(header);
    auto* tools = new QHBoxLayout(toolbar_);
    tools->setContentsMargins(0, 0, 0, 0);
    tools->setSpacing(6);
    previous_ = new QPushButton(tr("◀ Previous"), toolbar_);
    previous_->setToolTip(tr("The conflict before this one (Alt+Up)"));
    next_ = new QPushButton(tr("Next ▶"), toolbar_);
    next_->setToolTip(tr("The next conflict not yet resolved (Alt+Down)"));
    allOurs_ = new QPushButton(tr("Take Ours Everywhere"), toolbar_);
    allOurs_->setToolTip(tr("Every conflict in this file from ours. Changes only one side made "
                            "stay merged."));
    allTheirs_ = new QPushButton(tr("Take Theirs Everywhere"), toolbar_);
    allTheirs_->setToolTip(tr("Every conflict in this file from theirs. Changes only one side "
                              "made stay merged."));
    reset_ = new QPushButton(tr("Reset Choices"), toolbar_);
    reset_->setToolTip(tr("Forget the choices made here and start this file again"));
    restart_ = new QPushButton(tr("Start Over…"), toolbar_);
    restart_->setToolTip(tr("Put the file back as the merge left it, conflict markers and all — "
                            "even after it was marked resolved.\n\ngit checkout -m -- <file>"));
    save_ = new QPushButton(tr("Save"), toolbar_);
    save_->setToolTip(tr("Write the result to the file, leaving it conflicted. A conflict still "
                         "open is written with git's markers, to finish anywhere. (Ctrl+S)"));
    resolve_ = new QPushButton(tr("Save and Mark Resolved"), toolbar_);
    resolve_->setToolTip(tr("Write the result and stage it, which tells git the conflict is "
                            "resolved.\n\ngit add -- <file>"));
    applyRole(resolve_, QStringLiteral("primary"));
    tools->addWidget(previous_);
    tools->addWidget(next_);
    tools->addSpacing(12);
    tools->addWidget(allOurs_);
    tools->addWidget(allTheirs_);
    tools->addWidget(reset_);
    tools->addStretch(1);
    tools->addWidget(restart_);
    tools->addWidget(save_);
    tools->addWidget(resolve_);
    headerLayout->addWidget(toolbar_);
    layout->addWidget(header);

    connect(previous_, &QPushButton::clicked, this, [this] { goToConflict(-1); });
    connect(next_, &QPushButton::clicked, this, [this] { goToConflict(1); });
    connect(allOurs_, &QPushButton::clicked, this,
            [this] { document_->pickAllConflicts(Pick::Ours); });
    connect(allTheirs_, &QPushButton::clicked, this,
            [this] { document_->pickAllConflicts(Pick::Theirs); });
    connect(reset_, &QPushButton::clicked, this, [this] {
        result_.pane->finishEditing();
        document_->reset();
    });
    connect(restart_, &QPushButton::clicked, this, [this] {
        if (confirmDestructive(this, tr("Start over?"),
                               tr("Put %1 back as the merge left it?").arg(path()),
                               tr("What is in the file now, and any choices made here, are "
                                  "replaced by the conflict as git first wrote it."),
                               tr("Start Over"))) {
            emit restartRequested(path());
        }
    });
    connect(save_, &QPushButton::clicked, this, [this] { save(false); });
    connect(resolve_, &QPushButton::clicked, this, [this] { save(true); });

    const auto shortcut = [this](const QKeySequence& keys, auto&& slot) {
        auto* s = new QShortcut(keys, this);
        s->setContext(Qt::WidgetWithChildrenShortcut);
        connect(s, &QShortcut::activated, this, std::forward<decltype(slot)>(slot));
    };
    shortcut(QKeySequence(Qt::ALT | Qt::Key_Down), [this] { goToConflict(1); });
    shortcut(QKeySequence(Qt::ALT | Qt::Key_Up), [this] { goToConflict(-1); });
    shortcut(QKeySequence::Save, [this] {
        if (save_->isEnabled() && pages_->currentWidget() == columnsPage_) {
            save(false);
        }
    });

    // --- pages ------------------------------------------------------------
    pages_ = new QStackedWidget(this);
    layout->addWidget(pages_, 1);

    columnsPage_ = new QWidget(pages_);
    auto* columnsLayout = new QVBoxLayout(columnsPage_);
    columnsLayout->setContentsMargins(0, 0, 0, 0);
    ours_ = makeColumn(Column::Ours);
    result_ = makeColumn(Column::Result);
    theirs_ = makeColumn(Column::Theirs);
    pages_->addWidget(columnsPage_);

    // One scroll position for every column: their rows are laid out level.
    for (MergePane* pane : {ours_.pane, result_.pane, theirs_.pane}) {
        for (MergePane* other : {ours_.pane, result_.pane, theirs_.pane}) {
            if (other != pane) {
                connect(pane->verticalScrollBar(), &QScrollBar::valueChanged,
                        other->verticalScrollBar(), &QScrollBar::setValue);
            }
        }
        connect(pane, &MergePane::regionActivated, this,
                [this](std::size_t region) { setCurrent(region); });
        connect(pane, &MergePane::editRequested, this,
                [this](std::size_t region) { result_.pane->editRegion(region); });
    }
    connect(document_, &ConflictDocument::changed, this, [this] { updateState(); });

    wholePage_ = new QWidget(pages_);
    auto* whole = new QVBoxLayout(wholePage_);
    whole->setContentsMargins(32, 32, 32, 32);
    whole->setSpacing(10);
    wholeTitle_ = new QLabel(wholePage_);
    wholeTitle_->setObjectName(QStringLiteral("conflictPath"));
    wholeTitle_->setTextFormat(Qt::PlainText);
    wholeTitle_->setWordWrap(true);
    wholeDetail_ = new QLabel(wholePage_);
    wholeDetail_->setTextFormat(Qt::PlainText);
    wholeDetail_->setWordWrap(true);
    applyRole(wholeDetail_, QStringLiteral("muted"));
    auto* wholeButtons = new QHBoxLayout;
    keepOurs_ = new QPushButton(wholePage_);
    keepTheirs_ = new QPushButton(wholePage_);
    deleteFile_ = new QPushButton(tr("Delete the File"), wholePage_);
    deleteFile_->setToolTip(tr("Resolve it as deleted.\n\ngit rm -- <file>"));
    applyRole(deleteFile_, QStringLiteral("destructive"));
    wholeButtons->addWidget(keepOurs_);
    wholeButtons->addWidget(keepTheirs_);
    wholeButtons->addWidget(deleteFile_);
    wholeButtons->addStretch(1);
    whole->addWidget(wholeTitle_);
    whole->addWidget(wholeDetail_);
    whole->addLayout(wholeButtons);
    whole->addStretch(1);
    pages_->addWidget(wholePage_);
    connect(keepOurs_, &QPushButton::clicked, this,
            [this] { emit sideRequested(path(), Side::Ours); });
    connect(keepTheirs_, &QPushButton::clicked, this,
            [this] { emit sideRequested(path(), Side::Theirs); });
    connect(deleteFile_, &QPushButton::clicked, this, [this] {
        if (confirmDestructive(this, tr("Delete it?"), tr("Resolve %1 by deleting it?").arg(path()),
                               tr("The file is removed from the working copy and the commit "
                                  "that finishes the merge."),
                               tr("Delete"))) {
            emit sideRequested(path(), Side::Delete);
        }
    });

    messagePage_ = new QWidget(pages_);
    auto* message = new QVBoxLayout(messagePage_);
    message->setContentsMargins(32, 32, 32, 32);
    messageTitle_ = new QLabel(messagePage_);
    messageTitle_->setObjectName(QStringLiteral("conflictPath"));
    messageTitle_->setTextFormat(Qt::PlainText);
    messageTitle_->setWordWrap(true);
    messageDetail_ = new QLabel(messagePage_);
    messageDetail_->setTextFormat(Qt::PlainText);
    messageDetail_->setWordWrap(true);
    applyRole(messageDetail_, QStringLiteral("muted"));
    message->addWidget(messageTitle_);
    message->addWidget(messageDetail_);
    message->addStretch(1);
    pages_->addWidget(messagePage_);

    arrange();
    showMessage(tr("No file chosen"), tr("Pick a conflicted file from the list."));
}

ConflictEditor::ColumnView ConflictEditor::makeColumn(Column column) {
    ColumnView view;
    view.box = new QWidget(columnsPage_);
    auto* layout = new QVBoxLayout(view.box);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    view.title = new QLabel(view.box);
    view.title->setTextFormat(Qt::PlainText);
    view.title->setObjectName(column == Column::Ours     ? QStringLiteral("conflictOursTitle")
                              : column == Column::Theirs ? QStringLiteral("conflictTheirsTitle")
                                                         : QStringLiteral("conflictResultTitle"));
    view.title->setAttribute(Qt::WA_StyledBackground, true);
    view.title->setMinimumWidth(60);
    view.pane = new MergePane(document_, column, view.box);
    layout->addWidget(view.title);
    layout->addWidget(view.pane, 1);
    return view;
}

void ConflictEditor::setOperation(git::Operation operation) {
    operation_ = operation;
    updateTitles();
}

void ConflictEditor::setLayoutChoice(Layout layout) {
    if (layout == layout_ && outer_ != nullptr) {
        return;
    }
    layout_ = layout;
    layoutButtons_[static_cast<int>(layout)]->setChecked(true);
    result_.pane->finishEditing();
    arrange();
}

void ConflictEditor::arrange() {
    // Taken out of the old splitters before they go, so they survive it.
    for (ColumnView* view : {&ours_, &result_, &theirs_}) {
        view->box->setParent(columnsPage_);
    }
    delete outer_;
    outer_ = nullptr;

    switch (layout_) {
    case Layout::ThreeColumns:
        outer_ = new QSplitter(Qt::Horizontal, columnsPage_);
        outer_->addWidget(ours_.box);
        outer_->addWidget(result_.box);
        outer_->addWidget(theirs_.box);
        outer_->setSizes({1, 1, 1});
        break;
    case Layout::TwoColumns: {
        outer_ = new QSplitter(Qt::Vertical, columnsPage_);
        auto* sides = new QSplitter(Qt::Horizontal, outer_);
        sides->addWidget(ours_.box);
        sides->addWidget(theirs_.box);
        sides->setSizes({1, 1});
        sides->setChildrenCollapsible(false);
        outer_->addWidget(sides);
        outer_->addWidget(result_.box);
        outer_->setSizes({1, 1});
        break;
    }
    }
    outer_->setChildrenCollapsible(false);
    result_.box->show();
    // The columns scroll as one, so one scroll bar is enough: theirs, at the
    // right edge — and the result's too when it runs across the bottom.
    ours_.pane->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    result_.pane->setVerticalScrollBarPolicy(
        layout_ == Layout::TwoColumns ? Qt::ScrollBarAsNeeded : Qt::ScrollBarAlwaysOff);
    theirs_.pane->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    ours_.box->show();
    theirs_.box->show();
    columnsPage_->layout()->addWidget(outer_);
    outer_->show();
    // Even widths whatever the window's size: a splitter given {1, 1, 1}
    // shares out by those proportions.
    const int span = layout_ == Layout::TwoColumns ? columnsPage_->height() : columnsPage_->width();
    if (span > 0) {
        QList<int> sizes;
        for (int i = 0; i < outer_->count(); ++i) {
            sizes << span / outer_->count();
        }
        outer_->setSizes(sizes);
    }
    setCurrent(current_);
}

void ConflictEditor::showConflict(session::ConflictPtr file,
                                  std::optional<git::MergeResolution> kept, bool keptUnsaved) {
    result_.pane->finishEditing();
    current_.reset();
    if (!file) {
        document_->clear();
        showMessage(tr("No file chosen"), tr("Pick a conflicted file from the list."));
        return;
    }
    path_->setText(fromStd(file->path));
    if (file->binary || !file->bothSides()) {
        document_->setFile(file, git::MergeResolution{});
        showWholeFile(file);
        return;
    }
    git::MergeResolution resolution = kept ? std::move(*kept) : git::MergeResolution(file->merge);
    document_->setFile(file, std::move(resolution));
    if (keptUnsaved) {
        document_->markDirty();
    }
    toolbar_->setVisible(true);
    pages_->setCurrentWidget(columnsPage_);
    updateTitles();
    for (MergePane* pane : {ours_.pane, result_.pane, theirs_.pane}) {
        pane->verticalScrollBar()->setValue(0);
        pane->horizontalScrollBar()->setValue(0);
    }
    // Straight to the first conflict.
    if (!document_->conflicts().empty()) {
        current_ = document_->conflicts().back();
        goToConflict(1);
    }
    updateState();
}

void ConflictEditor::showWholeFile(const session::ConflictPtr& file) {
    toolbar_->setVisible(false);
    status_->clear();
    pages_->setCurrentWidget(wholePage_);
    const QString ours = file->head.branch.empty()
                             ? tr("Keep Ours")
                             : tr("Keep Ours (%1)").arg(fromStd(file->head.branch));
    keepOurs_->setText(ours);
    keepOurs_->setToolTip(
        tr("Resolve it with our whole version.\n\ngit checkout --ours -- <file>"));
    keepTheirs_->setText(tr("Keep Theirs"));
    keepTheirs_->setToolTip(
        tr("Resolve it with their whole version.\n\ngit checkout --theirs -- <file>"));
    keepOurs_->setVisible(file->ours.present);
    keepTheirs_->setVisible(file->theirs.present);
    deleteFile_->setVisible(!file->bothSides());
    if (file->binary && file->bothSides()) {
        wholeTitle_->setText(tr("Both sides changed this file, and it is not text"));
        wholeDetail_->setText(tr("It cannot be merged line by line. Keep one side's version "
                                 "whole — or merge it in the program that made it, save it, "
                                 "and stage it from Local Changes."));
    } else if (file->ours.present) {
        wholeTitle_->setText(tr("Ours changed this file; theirs deleted it"));
        wholeDetail_->setText(tr("Keep our version, or delete it as they did."));
    } else if (file->theirs.present) {
        wholeTitle_->setText(tr("Theirs changed this file; ours deleted it"));
        wholeDetail_->setText(tr("Keep their version, or leave it deleted."));
    } else {
        wholeTitle_->setText(tr("Both sides removed or renamed this file"));
        wholeDetail_->setText(tr("Nothing is left to merge: delete it to resolve the conflict."));
    }
}

void ConflictEditor::showMessage(const QString& title, const QString& detail) {
    result_.pane->finishEditing();
    toolbar_->setVisible(false);
    path_->clear();
    status_->clear();
    messageTitle_->setText(title);
    messageDetail_->setText(detail);
    pages_->setCurrentWidget(messagePage_);
}

QString ConflictEditor::path() const {
    const auto& file = document_->file();
    return file ? fromStd(file->path) : QString();
}

bool ConflictEditor::dirty() const {
    return document_->file() && document_->dirty();
}

std::optional<git::MergeResolution> ConflictEditor::resolution() const {
    result_.pane->finishEditing();
    if (!document_->file() || document_->regionCount() == 0) {
        return std::nullopt;
    }
    return document_->resolution();
}

void ConflictEditor::markSaved() {
    document_->markClean();
    updateState();
}

void ConflictEditor::updateTitles() {
    const auto& file = document_->file();
    if (!file) {
        return;
    }
    QString oursRole = tr("Ours");
    QString theirsRole = tr("Theirs");
    const QString branch = fromStd(file->head.branch);
    switch (operation_) {
    case git::Operation::RebaseInteractive:
    case git::Operation::RebaseApply:
        // Turned round mid-rebase: HEAD is what is being rebased onto.
        oursRole = tr("Ours — what you are rebasing onto");
        theirsRole = tr("Theirs — your commit being replayed");
        break;
    case git::Operation::CherryPick:
        oursRole = branch.isEmpty() ? tr("Ours") : tr("Ours — %1").arg(branch);
        theirsRole = tr("Theirs — the commit being picked");
        break;
    case git::Operation::Revert:
        oursRole = branch.isEmpty() ? tr("Ours") : tr("Ours — %1").arg(branch);
        theirsRole = tr("Theirs — the revert");
        break;
    case git::Operation::Merge:
    default:
        oursRole = branch.isEmpty() ? tr("Ours") : tr("Ours — %1").arg(branch);
        theirsRole = tr("Theirs — being merged in");
        break;
    }
    const auto titled = [](QLabel* label, const QString& role, const QString& commit) {
        label->setText(commit.isEmpty() ? role : QStringLiteral("%1   ·   %2").arg(role, commit));
        label->setToolTip(label->text());
    };
    titled(ours_.title, oursRole, commitLine(file->head));
    titled(theirs_.title, theirsRole, commitLine(file->incoming));
    result_.title->setText(tr("Result — what the file will hold"));
}

void ConflictEditor::updateState() {
    if (!document_->file() || pages_->currentWidget() != columnsPage_) {
        return;
    }
    const std::size_t total = document_->conflicts().size();
    const std::size_t open = document_->resolution().unresolvedCount();
    QString text;
    if (total == 0) {
        text = tr("No conflicting lines — git could not merge it whole, but every change merges");
    } else if (open == 0) {
        text = tr("All %n conflict(s) resolved", nullptr, static_cast<int>(total));
    } else {
        text = tr("%1 of %n conflict(s) left", nullptr, static_cast<int>(total)).arg(open);
    }
    if (document_->dirty()) {
        text += tr("  ·  not saved");
    }
    status_->setText(text);
    previous_->setEnabled(total > 0);
    next_->setEnabled(total > 0);
    allOurs_->setEnabled(total > 0);
    allTheirs_->setEnabled(total > 0);
    reset_->setEnabled(document_->dirty() || open < total);
    save_->setEnabled(document_->dirty());
    resolve_->setEnabled(open == 0);
    resolve_->setToolTip(open == 0 ? tr("Write the result and stage it, which tells git the "
                                        "conflict is resolved.\n\ngit add -- <file>")
                                   : tr("Resolve every conflict first — or Save to write it "
                                        "with markers and finish it elsewhere."));
}

void ConflictEditor::setCurrent(std::optional<std::size_t> region) {
    current_ = region;
    for (MergePane* pane : {ours_.pane, result_.pane, theirs_.pane}) {
        pane->setCurrentRegion(region);
    }
}

void ConflictEditor::goToConflict(int step) {
    const auto& conflicts = document_->conflicts();
    if (conflicts.empty()) {
        return;
    }
    std::optional<std::size_t> target;
    const std::size_t from = current_.value_or(step > 0 ? conflicts.back() : conflicts.front());
    if (step > 0) {
        // Forward goes to the next one still open, and only when all are
        // resolved, to the next one at all.
        target = document_->resolution().nextUnresolved(from);
        if (!target) {
            const auto after = std::upper_bound(conflicts.begin(), conflicts.end(), from);
            target = after != conflicts.end() ? *after : conflicts.front();
        }
    } else {
        const auto before = std::lower_bound(conflicts.begin(), conflicts.end(), from);
        target = before != conflicts.begin() ? *std::prev(before) : conflicts.back();
    }
    setCurrent(target);
    // A few rows of what comes before it stay in view.
    const MergePane* pane = ours_.pane;
    const int y = std::max(0, (document_->regionRow(*target) - 3) * pane->rowHeight());
    ours_.pane->verticalScrollBar()->setValue(y);
}

QString ConflictEditor::oursMarker() const {
    const auto& file = document_->file();
    if (!file) {
        return QStringLiteral("ours");
    }
    return file->head.branch.empty() ? QStringLiteral("HEAD") : fromStd(file->head.branch);
}

QString ConflictEditor::theirsMarker() const {
    const auto& file = document_->file();
    if (!file || file->incoming.shortId.empty()) {
        return QStringLiteral("theirs");
    }
    return QStringLiteral("%1 (%2)").arg(fromStd(file->incoming.shortId),
                                         fromStd(file->incoming.summary));
}

void ConflictEditor::save(bool markResolved) {
    result_.pane->finishEditing();
    if (!document_->file()) {
        return;
    }
    const std::string text =
        document_->resolution().text(oursMarker().toStdString(), theirsMarker().toStdString());
    emit saveRequested(path(), QByteArray::fromStdString(text), markResolved);
}

} // namespace gity::ui
