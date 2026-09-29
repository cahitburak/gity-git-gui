#include "WorkingCopyPanel.h"

#include "core/git/LfsLocks.h"
#include "ui/panels/Confirm.h"

#include "core/git/Staging.h"
#include "ui/diffview/DiffView.h"
#include "ui/panels/WorkingCopyDelegate.h"
#include "ui/theme/Theme.h"
#include "ui/theme/ToolbarIcons.h"
#include "ui/theme/Tokens.h"

#include <QApplication>
#include <algorithm>
#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QUrl>
#include <QHeaderView>
#include <QFileInfo>
#include <QMenu>
#include <QLabel>
#include <QCheckBox>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace gity::ui {
namespace {

QChar letterFor(const git::StatusEntry& entry, bool staged) {
    return QChar(git::stateLetter(staged ? entry.staged : entry.unstaged));
}

} // namespace

WorkingCopyPanel::WorkingCopyPanel(QWidget* parent) : QWidget(parent) {
    unstaged_ = buildList(QStringLiteral("unstaged"));
    staged_ = buildList(QStringLiteral("staged"));

    stageButton_ = new QPushButton(tr("Stage ▸"), this);
    unstageButton_ = new QPushButton(tr("◂ Unstage"), this);
    stageButton_->setEnabled(false);
    unstageButton_->setEnabled(false);

    // SPEC.md §2: two sections stacked in one column, each with a 29px header
    // carrying its file count and a text action.
    stagedHeader_ = new QLabel(this);
    unstagedHeader_ = new QLabel(this);
    for (QLabel* header : {stagedHeader_, unstagedHeader_}) {
        header->setObjectName(QStringLiteral("sectionLabel"));
        QFont label = header->font();
        label.setCapitalization(QFont::AllUppercase);
        label.setWeight(QFont::DemiBold);
        label.setPointSize(std::max(7, header->font().pointSize() - 2));
        header->setFont(label);
    }

    // SPEC.md §2: the title at the left of the header, a text action at the
    // right. A button rather than a clickable label — it is a real action and
    // should be reachable from the keyboard — styled flat so it reads as text.
    const auto buildHeader = [this](QLabel* title, const QString& actionText,
                                    QPushButton*& action) {
        auto* header = new QWidget(this);
        header->setFixedHeight(tokens::chromeListSectionHeader);
        header->setAutoFillBackground(true);
        header->setObjectName(QStringLiteral("sectionHeader"));

        auto* row = new QHBoxLayout(header);
        row->setContentsMargins(12, 0, 12, 0);
        row->addWidget(title);
        row->addStretch(1);

        action = new QPushButton(actionText, header);
        action->setFlat(true);
        action->setCursor(Qt::PointingHandCursor);
        action->setObjectName(QStringLiteral("sectionAction"));
        QFont actionFont = action->font();
        actionFont.setPointSize(std::max(7, action->font().pointSize() - 1));
        action->setFont(actionFont);
        action->setEnabled(false);
        row->addWidget(action);
        return header;
    };

    QWidget* stagedHeaderRow = buildHeader(stagedHeader_, tr("Unstage all"), unstageAll_);
    QWidget* unstagedHeaderRow = buildHeader(unstagedHeader_, tr("Stage all"), stageAll_);

    // Unstaged on top, staged beneath. Work moves down the column as it moves
    // towards a commit: you read the changes, stage what belongs, and the
    // commit box sits under the staged list it acts on.
    auto* listsColumn = new QVBoxLayout;
    listsColumn->setSpacing(0);
    listsColumn->addWidget(unstagedHeaderRow);
    listsColumn->addWidget(unstaged_, 2);
    listsColumn->addWidget(stagedHeaderRow);
    listsColumn->addWidget(staged_, 1);

    auto* actionRow = new QHBoxLayout;
    actionRow->addWidget(stageButton_);
    actionRow->addWidget(unstageButton_);
    actionRow->addStretch();
    listsColumn->addLayout(actionRow);

    auto* listsWidget = new QWidget(this);
    listsWidget->setLayout(listsColumn);

    diff_ = new DiffView(this);
    diff_->setPlaceholder(tr("Select a file to see its changes"));
    diff_->setSelectable(true);
    diff_->setStagingMode(true);

    // The same button both ways: what it does depends on which side you are
    // looking at, and saying so beats two buttons where one is always dead.
    stageSelectionButton_ = new QPushButton(tr("Stage selected lines"), this);
    stageSelectionButton_->setEnabled(false);

    message_ = new QPlainTextEdit(this);
    message_->setPlaceholderText(tr("Commit message"));
    message_->setMaximumHeight(110);

    commitButton_ = new QPushButton(tr("Commit"), this);
    commitButton_->setEnabled(false);
    // The action this screen exists for, so it carries the primary role. Styled
    // from gity.qss like everything else rather than with a sheet of its own.
    applyRole(commitButton_, QStringLiteral("primary"));

    amend_ = new QCheckBox(tr("Amend"), this);

    notice_ = new QLabel(this);
    notice_->setWordWrap(true);
    notice_->setVisible(false);

    commitReason_ = new QLabel(this);
    commitReason_->setWordWrap(true);
    commitReason_->setVisible(false);
    applyRole(commitReason_, QStringLiteral("note"));

    message_->setObjectName(QStringLiteral("commitMessage"));

    auto* commitRow = new QHBoxLayout;
    commitRow->addWidget(amend_, 0, Qt::AlignBottom);
    commitRow->addStretch();
    commitRow->addWidget(commitButton_, 0, Qt::AlignBottom);

    // SPEC.md §2: the commit box sits at the bottom of the *left* column,
    // under the lists — not across the width of the screen. Keeping it beside
    // the diff means "read the diff, then write the message" stays in one
    // place rather than crossing the window.
    auto* commitBox = new QWidget(this);
    auto* commitBoxLayout = new QVBoxLayout(commitBox);
    commitBoxLayout->setContentsMargins(12, 10, 12, 12);
    commitBoxLayout->addWidget(notice_);
    commitBoxLayout->addWidget(message_);
    commitBoxLayout->addLayout(commitRow);
    commitBoxLayout->addWidget(commitReason_);
    commitBox->setObjectName(QStringLiteral("commitBox"));

    auto* leftColumn = new QWidget(this);
    auto* leftLayout = new QVBoxLayout(leftColumn);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(0);
    leftLayout->addWidget(listsWidget, 1);
    leftLayout->addWidget(commitBox);

    // The stage-lines button belongs with the diff it acts on, not with the
    // lists — it stages what is selected in the diff pane.
    auto* rightColumn = new QWidget(this);
    auto* rightLayout = new QVBoxLayout(rightColumn);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(6);
    rightLayout->addWidget(diff_, 1);
    rightLayout->addWidget(stageSelectionButton_, 0, Qt::AlignLeft);

    auto* split = new QSplitter(Qt::Horizontal, this);
    split->setObjectName(QStringLiteral("stagingSplit"));
    split->addWidget(leftColumn);
    split->addWidget(rightColumn);
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 1);
    split->setSizes({tokens::panesWorkingCopyList,
                     tokens::windowWidth - tokens::panesWorkingCopyList});
    // Small floors, and no snapping shut (owner's request, 2026-09-28).
    leftColumn->setMinimumWidth(160);
    rightColumn->setMinimumWidth(120);
    split->setChildrenCollapsible(false);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(split, 1);

    const auto onSelection = [this](QTreeWidget* list, bool staged) {
        stageButton_->setEnabled(!unstaged_->selectedItems().isEmpty());
        unstageButton_->setEnabled(!staged_->selectedItems().isEmpty());
        const QStringList selected = selectionOf(list);
        if (!selected.isEmpty()) {
            viewingStaged_ = staged;
            diff_->setShowingStaged(staged);
            emit diffRequested(selected.first(), staged);
        }
    };
    // Lock and unlock live on the row rather than in a toolbar: the question
    // "is anyone else editing this?" is asked about a specific file, and the
    // answer is already on that row.
    for (QTreeWidget* list : {unstaged_, staged_}) {
        list->viewport()->installEventFilter(this);
        list->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(list, &QTreeWidget::customContextMenuRequested, this,
                [this, list](const QPoint& where) { showRowMenu(list, where); });
    }

    connect(unstaged_, &QTreeWidget::itemSelectionChanged, this,
            [this, onSelection] { onSelection(unstaged_, false); });
    connect(staged_, &QTreeWidget::itemSelectionChanged, this,
            [this, onSelection] { onSelection(staged_, true); });

    // "All" means every file in the section, not every selected one — that is
    // what the word says, and a header action that acted on the selection
    // would silently do less than it claims.
    const auto pathsIn = [](QTreeWidget* list) {
        QStringList paths;
        for (int i = 0; i < list->topLevelItemCount(); ++i) {
            const QString path = list->topLevelItem(i)->data(0, PathRole).toString();
            if (!path.isEmpty()) {
                paths << path;
            }
        }
        return paths;
    };
    connect(stageAll_, &QPushButton::clicked, this, [this, pathsIn] {
        const QStringList paths = withRenameSources(pathsIn(unstaged_), false);
        if (!paths.isEmpty()) {
            emit stageRequested(paths);
        }
    });
    connect(unstageAll_, &QPushButton::clicked, this, [this, pathsIn] {
        const QStringList paths = withRenameSources(pathsIn(staged_), true);
        if (!paths.isEmpty()) {
            emit unstageRequested(paths);
        }
    });

    connect(stageButton_, &QPushButton::clicked, this,
            [this] { emit stageRequested(withRenameSources(selectionOf(unstaged_), false)); });
    connect(unstageButton_, &QPushButton::clicked, this,
            [this] { emit unstageRequested(withRenameSources(selectionOf(staged_), true)); });
    connect(unstaged_, &QTreeWidget::itemDoubleClicked, this,
            [this] { emit stageRequested(withRenameSources(selectionOf(unstaged_), false)); });
    connect(staged_, &QTreeWidget::itemDoubleClicked, this,
            [this] { emit unstageRequested(withRenameSources(selectionOf(staged_), true)); });

    connect(diff_, &DiffView::selectionChanged, this, [this](int count) {
        stageSelectionButton_->setEnabled(count > 0);
        if (count == 0) {
            stageSelectionButton_->setText(viewingStaged_ ? tr("Unstage selected lines")
                                                          : tr("Stage selected lines"));
        } else {
            stageSelectionButton_->setText(
                viewingStaged_ ? tr("Unstage %n selected line(s)", nullptr, count)
                               : tr("Stage %n selected line(s)", nullptr, count));
        }
    });
    connect(diff_, &DiffView::stageSelectionRequested, stageSelectionButton_,
            &QPushButton::click);
    stageSelectionButton_->setToolTip(
        tr("From the keyboard, in the diff: ↑/↓ move, Shift extends, Space selects a line, "
           "Enter stages the selection or the hunk, Delete discards the hunk."));
    connect(stageSelectionButton_, &QPushButton::clicked, this, [this] {
        const git::FileDiff* current = diff_->diff();
        if (current == nullptr || diff_->selection().empty()) {
            return;
        }
        // The patch is built here, from exactly what the user is looking at.
        const std::string patch =
            git::buildPartialPatch(*current, diff_->selection(), viewingStaged_);
        if (!patch.empty()) {
            emit applyPatchRequested(QString::fromStdString(patch));
        }
    });

    // The pick gutter acts on one line, immediately.
    connect(diff_, &DiffView::lineToggled, this, [this](std::uint32_t lineIndex) {
        const git::FileDiff* current = diff_->diff();
        if (current == nullptr) {
            return;
        }
        git::LineSelection one;
        one.insert(lineIndex);
        const std::string patch = git::buildPartialPatch(*current, one, viewingStaged_);
        if (!patch.empty()) {
            emit applyPatchRequested(QString::fromStdString(patch));
        }
    });

    connect(diff_, &DiffView::hunkStageRequested, this, [this](std::size_t hunkIndex) {
        const git::FileDiff* current = diff_->diff();
        if (current == nullptr) {
            return;
        }
        const std::string patch = git::buildPartialPatch(
            *current, git::linesOfHunk(*current, hunkIndex), viewingStaged_);
        if (!patch.empty()) {
            emit applyPatchRequested(QString::fromStdString(patch));
        }
    });

    connect(diff_, &DiffView::hunkDiscardRequested, this, [this](std::size_t hunkIndex) {
        const git::FileDiff* current = diff_->diff();
        if (current == nullptr) {
            return;
        }
        // Discard throws away work with no reflog to recover it from, so it
        // asks first. Staging never does — it is reversible.
        if (!confirmDestructive(
                this, tr("Discard hunk?"),
                tr("Discard this hunk from %1?").arg(QString::fromStdString(current->path)),
                tr("This rewrites the file on disk and cannot be undone — there is no reflog "
                   "for an uncommitted edit."),
                tr("Discard Hunk"))) {
            return;
        }
        const std::string patch = git::buildPartialPatch(
            *current, git::linesOfHunk(*current, hunkIndex), true);
        if (!patch.empty()) {
            emit discardHunkRequested(QString::fromStdString(patch));
        }
    });

    connect(message_, &QPlainTextEdit::textChanged, this,
            &WorkingCopyPanel::updateCommitButton);
    // Amending can be only a new message, so ticking it changes whether the
    // button needs anything staged.
    connect(amend_, &QCheckBox::toggled, this, &WorkingCopyPanel::updateCommitButton);
    connect(commitButton_, &QPushButton::clicked, this, [this] {
        emit commitRequested(message_->toPlainText(), amend_->isChecked());
    });
}

QTreeWidget* WorkingCopyPanel::buildList(const QString& title) {
    auto* list = new QTreeWidget(this);
    list->setColumnCount(1);
    list->setHeaderHidden(true);
    list->setRootIsDecorated(false);
    list->setUniformRowHeights(true);
    list->setMouseTracking(true); // the delegate paints a hover state
    list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    list->setItemDelegate(new WorkingCopyDelegate(title == QStringLiteral("staged"), list));
    // Named, and styled from gity.qss with everything else. A sheet set here
    // would override the application's and become a second place a colour
    // lives — which is how the two drift apart.
    list->setObjectName(QStringLiteral("workingCopyList"));
    return list;
}

QStringList WorkingCopyPanel::withRenameSources(const QStringList& paths, bool staged) const {
    // A rename is two paths to git — the new one appearing and the old one
    // going — and the list shows only the new. Staging just that half added a
    // copy and left the old file's deletion behind as a second change to
    // stage; unstaging it did the reverse.
    QStringList expanded = paths;
    if (!status_) {
        return expanded;
    }
    for (const QString& path : paths) {
        const git::StatusEntry* entry = status_->find(path.toStdString());
        if (entry == nullptr || entry->oldPath.empty()) {
            continue;
        }
        const git::FileState state = staged ? entry->staged : entry->unstaged;
        if (state == git::FileState::Renamed) {
            const QString source = QString::fromStdString(entry->oldPath);
            if (!expanded.contains(source)) {
                expanded << source;
            }
        }
    }
    return expanded;
}

QStringList WorkingCopyPanel::selectionOf(QTreeWidget* list) const {
    QStringList paths;
    for (const QTreeWidgetItem* item : list->selectedItems()) {
        const QString path = item->data(0, PathRole).toString();
        if (!path.isEmpty()) {
            paths << path;
        }
    }
    return paths;
}

void WorkingCopyPanel::fillList(QTreeWidget* list, bool staged) {
    list->clear();
    if (!status_) {
        return;
    }

    int files = 0;
    for (const git::StatusEntry& entry : status_->entries) {
        const git::StatusEntry* primary = &entry;
        // A conflicted file has neither a staged nor an unstaged delta of its
        // own — git is holding three versions of it rather than one — so the
        // plain relevance test drops it from both lists, hiding it on the very
        // screen someone opens to resolve it. It belongs in the working copy:
        // resolving it is the next thing to do.
        const bool relevant =
            staged ? primary->hasStaged() : (primary->hasUnstaged() || primary->conflicted);
        if (!relevant) {
            continue;
        }
        ++files;

        const QString path = QString::fromStdString(primary->path);
        const int slash = static_cast<int>(path.lastIndexOf(QChar('/')));

        auto* item = new QTreeWidgetItem(list);
        item->setData(0, PathRole, path);
        item->setData(0, NameRole, slash < 0 ? path : path.mid(slash + 1));
        item->setData(0, DirectoryRole, slash < 0 ? QString() : path.left(slash + 1));
        item->setData(0, StatusRole, QString(letterFor(*primary, staged)));

        // The chip carries the thing a reviewer needs to notice, and every
        // coloured chip also carries a word — never colour alone.
        // One chip, chosen by what matters most — an unresolved conflict
        // first, then who holds the file, then partial staging — and every
        // state that applies still named on the second line. Assigning them
        // in turn let a lock hide a conflict (2026-09 UI review).
        const auto lock = locks_ ? git::lockFor(*locks_, primary->path) : std::nullopt;
        const bool partial = primary->hasStaged() && primary->hasUnstaged();
        QStringList notes;
        if (primary->conflicted) {
            notes << tr("resolve before staging");
        }
        if (lock) {
            notes << tr("locked by %1").arg(QString::fromStdString(lock->owner));
        }
        if (partial) {
            notes << tr("some lines staged");
        }
        if (primary->conflicted) {
            item->setData(0, ChipRole, tr("conflict"));
            item->setData(0, ChipKindRole, static_cast<int>(ChipKind::Remove));
        } else if (lock) {
            item->setData(0, ChipRole, tr("locked"));
            item->setData(0, ChipKindRole, static_cast<int>(ChipKind::Warn));
        } else if (partial) {
            item->setData(0, ChipRole, tr("partial"));
            item->setData(0, ChipKindRole, static_cast<int>(ChipKind::Neutral));
        }
        item->setData(0, WarningRole, primary->conflicted || lock.has_value());
        if (!notes.isEmpty()) {
            item->setData(0, SubtitleRole, notes.join(QStringLiteral(" · ")));
        }
    }

    QLabel* header = staged ? stagedHeader_ : unstagedHeader_;
    header->setText(staged ? tr("Staged · %n file(s)", nullptr, files)
                           : tr("Working copy · %n file(s)", nullptr, files));
    if (QPushButton* action = staged ? unstageAll_ : stageAll_; action != nullptr) {
        action->setEnabled(files > 0);
    }
}

bool WorkingCopyPanel::eventFilter(QObject* watched, QEvent* event) {
    for (QTreeWidget* list : {unstaged_, staged_}) {
        if (list == nullptr || watched != list->viewport()) {
            continue;
        }
        if (event->type() != QEvent::MouseButtonPress &&
            event->type() != QEvent::MouseButtonRelease &&
            event->type() != QEvent::MouseButtonDblClick) {
            return false;
        }
        const auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() != Qt::LeftButton) {
            return false;
        }
        QTreeWidgetItem* item = list->itemAt(mouse->position().toPoint());
        if (item == nullptr) {
            return false;
        }
        // Generous: the box is 13px, and a click just beside it meant it.
        const QRect box = WorkingCopyDelegate::checkboxRect(list->visualItemRect(item))
                              .adjusted(-8, -8, 6, 8);
        if (!box.contains(mouse->position().toPoint())) {
            return false;
        }
        // Acted on at release, and every event of the click swallowed, so the
        // selection stays what it was: ticking one of five selected files
        // stages all five, which is what ticking a selected row means.
        if (event->type() == QEvent::MouseButtonRelease) {
            const QString path = item->data(0, PathRole).toString();
            const QStringList selected = selectionOf(list);
            const QStringList targets =
                item->isSelected() && !selected.isEmpty() ? selected : QStringList{path};
            if (list == unstaged_) {
                emit stageRequested(withRenameSources(targets, false));
            } else {
                emit unstageRequested(withRenameSources(targets, true));
            }
        }
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

void WorkingCopyPanel::showRowMenu(QTreeWidget* list, const QPoint& where) {
    QTreeWidgetItem* item = list->itemAt(where);
    if (item == nullptr) {
        return;
    }
    const QString path = item->data(0, PathRole).toString();
    if (path.isEmpty()) {
        return;
    }
    // A right-click inside the selection acts on the selection, as in every
    // file manager; outside it, it selects just that row first. Setting the
    // current item outright used to drop a multiple selection to one.
    if (item->isSelected()) {
        list->setCurrentItem(item, 0, QItemSelectionModel::NoUpdate);
    } else {
        list->setCurrentItem(item);
    }
    const bool staged = list == staged_;

    using icons::Glyph;
    const auto glyph = [](Glyph which) { return icons::themed(which); };

    QMenu menu(this);
    const QStringList selected = selectionOf(list);
    const QStringList targets = selected.isEmpty() ? QStringList{path} : selected;
    const int count = static_cast<int>(targets.size());
    const bool single = count == 1;
    const QString subject = single ? QFileInfo(targets.first()).fileName()
                                   : tr("%n file(s)", nullptr, count);

    QAction* stage = staged ? menu.addAction(tr("Unstage %1").arg(subject))
                            : menu.addAction(tr("Stage %1").arg(subject));
    QAction* stash = menu.addAction(glyph(Glyph::Stash), tr("Stash %1").arg(subject));
    QAction* discard =
        menu.addAction(glyph(Glyph::Discard), tr("Discard Changes to %1…").arg(subject));
    menu.addSeparator();

    QAction* open = nullptr;
    QAction* reveal = nullptr;
    if (single && !workdir_.isEmpty()) {
        open = menu.addAction(tr("Open %1").arg(subject));
        reveal = menu.addAction(tr("Show in Folder"));
        // A deleted file has nothing to open.
        open->setEnabled(QFileInfo::exists(QDir(workdir_).filePath(path)));
    }
    QAction* copyPath = menu.addAction(glyph(Glyph::Copy), single ? tr("Copy Path")
                                                                  : tr("Copy Paths"));
    QAction* copyFull = workdir_.isEmpty()
                            ? nullptr
                            : menu.addAction(single ? tr("Copy Full Path") : tr("Copy Full Paths"));
    menu.addSeparator();

    QAction* lock = nullptr;
    QAction* unlock = nullptr;
    // Locks are asked about one file at a time: whether someone else holds
    // it is a question about that file.
    if (single) {
        if (!lockingSupported_) {
            // Said plainly rather than by showing a disabled item with no reason.
            QAction* note = menu.addAction(tr("This remote does not support LFS locking"));
            note->setEnabled(false);
        } else {
            const auto held = locks_ ? git::lockFor(*locks_, path.toStdString()) : std::nullopt;
            if (held) {
                unlock = menu.addAction(glyph(Glyph::Unlock),
                                        tr("Unlock — held by %1")
                                            .arg(QString::fromStdString(held->owner)));
            } else {
                lock = menu.addAction(glyph(Glyph::Lock), tr("Lock %1").arg(subject));
            }
        }
    }

    menu.setToolTipsVisible(true);
    describeCommand(stage,
                    staged ? tr("Take these out of the next commit; the edits stay.")
                           : tr("Put these in the next commit."),
                    staged ? QStringLiteral("restore --staged -- <files>")
                           : QStringLiteral("add -- <files>"));
    describeCommand(stash, tr("Set these files' changes aside, leaving the others. New files "
                              "go too."),
                    QStringLiteral("stash push --include-untracked -- <files>"));
    describeCommand(discard, tr("Throw away uncommitted changes. New files are deleted. Edit ▸ "
                                "Undo brings them back."),
                    QStringLiteral("restore --source=HEAD --staged --worktree -- <files>"));
    describeCommand(lock, tr("Tell the team you are editing this file."),
                    QStringLiteral("lfs lock -- %1").arg(path));
    describeCommand(unlock, tr("Release the lock so others can edit it."),
                    QStringLiteral("lfs unlock -- %1").arg(path));

    QAction* chosen = menu.exec(list->viewport()->mapToGlobal(where));
    if (chosen == nullptr) {
        return;
    }
    if (chosen == stage) {
        if (staged) {
            emit unstageRequested(withRenameSources(targets, true));
        } else {
            emit stageRequested(withRenameSources(targets, false));
        }
        return;
    }
    if (chosen == stash) {
        emit stashRequested(withRenameSources(targets, staged));
        return;
    }
    if (chosen == discard) {
        // The one action here that destroys work, so it says exactly what goes
        // and does not offer a default of yes.
        QString names;
        for (int i = 0; i < std::min(count, 8); ++i) {
            names += QStringLiteral("\n  ") + targets[i];
        }
        if (count > 8) {
            names += QStringLiteral("\n  ") + tr("…and %n more", nullptr, count - 8);
        }
        if (confirmDestructive(
                this, tr("Discard changes?"),
                tr("Return %n file(s) to the last commit?", nullptr, count) + QChar('\n') + names,
                tr("Their uncommitted changes go, staged ones included, and new files are "
                   "deleted. Edit ▸ Undo brings them back."),
                tr("Discard"))) {
            emit discardRequested(targets);
        }
        return;
    }
    if (chosen == copyPath || chosen == copyFull) {
        QStringList lines;
        for (const QString& target : targets) {
            lines << (chosen == copyFull ? QDir::toNativeSeparators(QDir(workdir_).filePath(target))
                                         : target);
        }
        QApplication::clipboard()->setText(lines.join(QChar('\n')));
        return;
    }
    if (chosen == open) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QDir(workdir_).filePath(path)));
        return;
    }
    if (chosen == reveal) {
        // The folder, not the file: opening a file's URL would launch it.
        QDesktopServices::openUrl(
            QUrl::fromLocalFile(QFileInfo(QDir(workdir_).filePath(path)).absolutePath()));
        return;
    }
    if (chosen == lock) {
        emit lockRequested(path);
    } else if (chosen == unlock) {
        // git-lfs refuses to unlock a file that still has uncommitted changes,
        // and it is right to: unlocking announces you are finished with a file
        // you are visibly still editing. Asked here rather than letting the
        // refusal arrive as an error with no offer of what to do about it.
        const git::StatusEntry* entry =
            status_ ? status_->find(path.toStdString()) : nullptr;
        const bool dirty = entry != nullptr && (entry->hasStaged() || entry->hasUnstaged());
        if (dirty) {
            const auto answer = QMessageBox::question(
                this, tr("Unlock anyway?"),
                tr("%1 still has uncommitted changes. Unlocking tells the rest of the team "
                   "you are finished with it, and they may start editing.\n\nCommit or "
                   "discard your changes first if you are not.")
                    .arg(QFileInfo(path).fileName()),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (answer != QMessageBox::Yes) {
                return;
            }
            emit unlockRequested(path, true);
            return;
        }
        emit unlockRequested(path, false);
    }
}

void WorkingCopyPanel::setLocks(session::LockListPtr locks, bool supported) {
    locks_ = std::move(locks);
    lockingSupported_ = supported;
    rebuild();
}

void WorkingCopyPanel::setStatus(session::StatusPtr status) {
    status_ = std::move(status);
    rebuild();
}

void WorkingCopyPanel::rebuild() {
    // What was selected, so a refresh does not throw it away. Every stage,
    // unstage, discard and lock refreshes, and landing back on the first file
    // each time made working down a long list mean scrolling back to your
    // place after every click.
    QTreeWidget* focusList = staged_->selectedItems().isEmpty() ? unstaged_ : staged_;
    const QStringList wasSelected = selectionOf(focusList);
    int wasRow = -1;
    if (QTreeWidgetItem* current = focusList->currentItem(); current != nullptr) {
        wasRow = focusList->indexOfTopLevelItem(current);
    }

    // Filled with signals blocked: clearing a list announces an empty
    // selection, and that would request a diff for every intermediate state.
    for (QTreeWidget* list : {unstaged_, staged_}) {
        const QSignalBlocker quiet(list);
        fillList(list, list == staged_);
    }
    stageButton_->setEnabled(false);
    unstageButton_->setEnabled(false);
    updateCommitButton();

    // The same files if they are still in the same list; otherwise whatever
    // now sits where the selection was — after staging a file, that is the
    // next one down, which is where you were going anyway.
    bool restored = false;
    for (int i = 0; i < focusList->topLevelItemCount(); ++i) {
        QTreeWidgetItem* item = focusList->topLevelItem(i);
        if (wasSelected.contains(item->data(0, PathRole).toString())) {
            if (!restored) {
                focusList->setCurrentItem(item);
                restored = true;
            }
            item->setSelected(true);
        }
    }
    if (!restored && wasRow >= 0 && focusList->topLevelItemCount() > 0) {
        focusList->setCurrentItem(
            focusList->topLevelItem(std::min(wasRow, focusList->topLevelItemCount() - 1)));
        restored = true;
    }
    if (restored) {
        return;
    }

    // Otherwise land on the first unstaged change so the diff pane has
    // content. Staging is a read-then-act loop, and opening on an empty pane
    // wastes the first step of it every time.
    if (unstaged_->topLevelItemCount() > 0) {
        unstaged_->setCurrentItem(unstaged_->topLevelItem(0));
    } else if (staged_->topLevelItemCount() > 0) {
        staged_->setCurrentItem(staged_->topLevelItem(0));
    } else {
        diff_->clearDiff();
    }
}

void WorkingCopyPanel::updateCommitButton() {
    const bool hasMessage = !message_->toPlainText().trimmed().isEmpty();
    const int staged = staged_->topLevelItemCount();
    // An amend needs nothing staged: rewording the last commit is the most
    // common reason to reach for it.
    const bool amending = amend_->isChecked();
    const int conflicts = status_ ? static_cast<int>(status_->conflictedCount()) : 0;

    // Why Commit cannot be pressed, said beside it rather than left for a
    // disabled button to be puzzled over. git still decides; this only
    // explains the cases known in advance (2026-09 UI review).
    QString reason;
    if (conflicts > 0) {
        reason = tr("Resolve %n conflicted file(s) and stage the results first.", nullptr,
                    conflicts);
    } else if (staged == 0 && !amending) {
        reason = tr("Stage the changes to include — select files and Stage, or Stage all.");
    } else if (!hasMessage) {
        reason = amending ? tr("Write the amended commit's message.")
                          : tr("Write a message to commit.");
    }
    commitReason_->setText(reason);
    commitReason_->setVisible(!reason.isEmpty() && status_ && !status_->entries.empty());
    commitButton_->setEnabled(reason.isEmpty());
    if (amending) {
        commitButton_->setText(tr("Amend Last Commit"));
    } else {
        commitButton_->setText(staged > 0 ? tr("Commit %n file(s)", nullptr, staged)
                                         : tr("Commit"));
    }
    commitButton_->setToolTip(staged > 0 || amending ? QString() : tr("Nothing is staged"));
}

void WorkingCopyPanel::setDiff(session::FileDiffPtr diff, session::HunkStagingPtr staging) {
    diff_->setDiff(std::move(diff), std::move(staging));
}

void WorkingCopyPanel::setNotice(const QString& text, bool isError) {
    notice_->setVisible(!text.isEmpty());
    notice_->setText(text);
    // A role rather than a colour, so the stylesheet keeps owning both and the
    // notice follows a theme change like everything else.
    applyRole(notice_, isError ? QStringLiteral("error") : QStringLiteral("muted"));
}

void WorkingCopyPanel::showCommitted(const QString& summary) {
    message_->clear();
    // One amend, not a mode. Left ticked, the next ordinary commit silently
    // rewrote the one before it.
    amend_->setChecked(false);
    setNotice(tr("Committed %1").arg(summary), false);
}

void WorkingCopyPanel::showCommitFailed(const QString& message) {
    setNotice(message, true);
}

void WorkingCopyPanel::clearAll(bool keepMessage) {
    status_.reset();
    unstaged_->clear();
    staged_->clear();
    diff_->clearDiff();
    if (!keepMessage) {
        message_->clear();
        amend_->setChecked(false);
    }
    setNotice({}, false);
    updateCommitButton();
}

QString WorkingCopyPanel::draftMessage() const {
    return message_->toPlainText();
}

bool WorkingCopyPanel::draftAmend() const {
    return amend_->isChecked();
}

void WorkingCopyPanel::restoreDraft(const QString& message, bool amend) {
    message_->setPlainText(message);
    amend_->setChecked(amend);
}

} // namespace gity::ui
