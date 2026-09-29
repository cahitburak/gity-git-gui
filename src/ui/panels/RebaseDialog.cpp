#include "RebaseDialog.h"

#include "ui/diffview/DiffView.h"
#include "ui/panels/CommitDetailPanel.h"
#include "ui/panels/Confirm.h"

#include "ui/theme/Tokens.h"

#include <QApplication>
#include <QCoreApplication>
#include <QKeyEvent>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QShortcut>
#include <QSplitter>
#include <QStyledItemDelegate>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace gity::ui {
namespace {

constexpr int kActionRole = Qt::UserRole + 1;
constexpr int kOidRole = Qt::UserRole + 2;
constexpr int kSubjectRole = Qt::UserRole + 3;
constexpr int kMessageRole = Qt::UserRole + 4; ///< Reword: the new message.

struct ActionChoice {
    git::RebaseAction action;
    const char* label;
    const char* explanation;
    const char* key; ///< Single-key shortcut on the list: git's own todo letter.
};

/// Ordered from harmless to destructive, which is also how often they are
/// wanted.
const ActionChoice kChoices[] = {
    {git::RebaseAction::Pick, QT_TR_NOOP("Keep"), QT_TR_NOOP("Keep the commit as it is."), "P"},
    {git::RebaseAction::Reword, QT_TR_NOOP("Reword"),
     QT_TR_NOOP("Keep the changes, write a new message."), "R"},
    {git::RebaseAction::Edit, QT_TR_NOOP("Stop to edit"),
     QT_TR_NOOP("Stop at this commit with its changes staged in Local Changes, to change them "
                "before carrying on."),
     "E"},
    {git::RebaseAction::Squash, QT_TR_NOOP("Squash"),
     QT_TR_NOOP("Fold into the commit below, keeping both messages."), "S"},
    {git::RebaseAction::Fixup, QT_TR_NOOP("Fixup"),
     QT_TR_NOOP("Fold into the commit below, discarding this message."), "F"},
    {git::RebaseAction::Drop, QT_TR_NOOP("Drop"),
     QT_TR_NOOP("Leave the commit out — its changes are gone from the branch."), "D"},
};

const ActionChoice& choiceFor(git::RebaseAction action) {
    for (const ActionChoice& choice : kChoices) {
        if (choice.action == action) {
            return choice;
        }
    }
    return kChoices[0];
}

/// How much each action changes, as a colour — the same scale as everywhere
/// else: neutral keeps, blue and purple change a commit, amber merges two
/// away, red removes one.
QString chipKind(git::RebaseAction action) {
    switch (action) {
    case git::RebaseAction::Reword:
        return QStringLiteral("info");
    case git::RebaseAction::Edit:
        return QStringLiteral("meta");
    case git::RebaseAction::Squash:
    case git::RebaseAction::Fixup:
        return QStringLiteral("warn");
    case git::RebaseAction::Drop:
        return QStringLiteral("remove");
    case git::RebaseAction::Pick:
        break;
    }
    return QStringLiteral("neutral");
}

struct ChipColours {
    QColor text;
    QColor fill;
    QColor border;
};

ChipColours coloursFor(git::RebaseAction action) {
    switch (action) {
    case git::RebaseAction::Reword:
        return {tokens::semanticInfo, tokens::fillInfoChip, tokens::fillInfoChipBorder};
    case git::RebaseAction::Edit:
        return {tokens::semanticMeta, tokens::fillMetaChip, tokens::fillMetaChipBorder};
    case git::RebaseAction::Squash:
    case git::RebaseAction::Fixup:
        return {tokens::semanticWarn, tokens::fillWarnChip, tokens::fillWarnChipBorder};
    case git::RebaseAction::Drop:
        return {tokens::semanticRemove, tokens::fillRemoveChip, tokens::fillRemoveChipBorder};
    case git::RebaseAction::Pick:
        break;
    }
    return {tokens::textDim, tokens::fillNeutralChip, tokens::fillNeutralChipBorder};
}

/// The action column as a chip: coloured by what the action does, and always
/// carrying its word, so colour is never the only signal.
class ActionChipDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override {
        if (index.column() != 0) {
            QStyledItemDelegate::paint(painter, option, index);
            return;
        }
        // The row's background and selection, without its text: the chip
        // carries the word. (Handing the cleared option to the base paint
        // would not work — it re-reads the text from the index.)
        QStyleOptionViewItem plain = option;
        initStyleOption(&plain, index);
        plain.text.clear();
        const QWidget* widget = option.widget;
        QStyle* style = widget != nullptr ? widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &plain, painter, widget);

        const auto action = static_cast<git::RebaseAction>(index.data(kActionRole).toInt());
        const ChipColours colours = coloursFor(action);
        const QString label = index.data(Qt::DisplayRole).toString();
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        QFont font = option.font;
        font.setWeight(QFont::DemiBold);
        painter->setFont(font);
        const int width = QFontMetrics(font).horizontalAdvance(label) + 16;
        const QRectF chip(option.rect.x() + 6, option.rect.center().y() - 9, width, 18);
        painter->setPen(QPen(colours.border, 1));
        painter->setBrush(colours.fill);
        painter->drawRoundedRect(chip.adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);
        painter->setPen(colours.text);
        painter->drawText(chip, Qt::AlignCenter, label);
        painter->restore();
    }
};

} // namespace

RebaseDialog::RebaseDialog(session::RepoSession& session, const QString& baseOid,
                           const QString& baseSubject, QWidget* parent)
    : QDialog(parent), session_(session), baseOid_(baseOid) {
    setWindowTitle(tr("Interactive Rebase"));
    setModal(true);
    setMinimumSize(760, 560);
    resize(980, 720);

    auto* layout = new QVBoxLayout(this);

    auto* intro = new QLabel(
        tr("Replaying the commits above %1 — \"%2\".\n"
           "They are listed newest first, the way history reads. Squash and fixup fold into "
           "the commit shown below them.")
            .arg(baseOid.left(8), baseSubject),
        this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    list_ = new QTreeWidget(this);
    list_->setColumnCount(3);
    list_->setHeaderLabels({tr("Action"), tr("Commit"), tr("Subject")});
    list_->setRootIsDecorated(false);
    list_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    list_->setItemDelegate(new ActionChipDelegate(list_));
    list_->header()->setSectionResizeMode(0, QHeaderView::Fixed);
    list_->header()->resizeSection(0, 124);
    list_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    list_->header()->setSectionResizeMode(2, QHeaderView::Stretch);

    auto* controls = new QHBoxLayout;
    up_ = new QPushButton(tr("Move up"), this);
    down_ = new QPushButton(tr("Move down"), this);
    controls->addWidget(up_);
    controls->addWidget(down_);
    controls->addStretch(1);
    for (const ActionChoice& choice : kChoices) {
        const git::RebaseAction action = choice.action;
        auto* button = new QPushButton(action == git::RebaseAction::Reword
                                           ? tr("Reword…")
                                           : QCoreApplication::translate(
                                                 "gity::ui::RebaseDialog", choice.label),
                                       this);
        // Styled by gity.qss from this property — the same chip colours as
        // the list, so the button says how much it changes before it is
        // pressed.
        button->setProperty("gityChip", chipKind(action));
        button->setToolTip(
            QStringLiteral("%1  (%2)").arg(
                QCoreApplication::translate("gity::ui::RebaseDialog", choice.explanation),
                QString::fromLatin1(choice.key)));
        if (action == git::RebaseAction::Reword) {
            connect(button, &QPushButton::clicked, this, &RebaseDialog::reword);
        } else {
            connect(button, &QPushButton::clicked, this,
                    [this, action] { applyActionToSelection(action); });
        }
        controls->addWidget(button);

    }
    // The letters `git rebase -i` uses — p, r, e, s, f, d — taken on the
    // list itself, before its type-to-search can claim the letter.
    list_->installEventFilter(this);
    auto* upKey = new QShortcut(QKeySequence(Qt::ALT | Qt::Key_Up), list_);
    connect(upKey, &QShortcut::activated, this, [this] { move(-1); });
    auto* downKey = new QShortcut(QKeySequence(Qt::ALT | Qt::Key_Down), list_);
    connect(downKey, &QShortcut::activated, this, [this] { move(1); });

    // What the selected commit changes, underneath.
    auto* changes = new QWidget(this);
    auto* changesLayout = new QVBoxLayout(changes);
    changesLayout->setContentsMargins(0, 6, 0, 0);
    changesTitle_ = new QLabel(tr("Select a commit to see its changes."), changes);
    applyRole(changesTitle_, QStringLiteral("muted"));
    changesLayout->addWidget(changesTitle_);
    auto* changesSplit = new QSplitter(Qt::Horizontal, changes);
    files_ = new ChangedFilesList(changesSplit);
    diff_ = new DiffView(changesSplit);
    diff_->setStagingMode(false);
    diff_->setPlaceholder(tr("Select a file to see its diff"));
    changesSplit->addWidget(files_);
    changesSplit->addWidget(diff_);
    changesSplit->setStretchFactor(1, 1);
    changesSplit->setSizes({260, 640});
    changesSplit->setChildrenCollapsible(false);
    changesLayout->addWidget(changesSplit, 1);

    auto* listPane = new QWidget(this);
    auto* listLayout = new QVBoxLayout(listPane);
    listLayout->setContentsMargins(0, 0, 0, 0);
    listLayout->addWidget(list_, 1);
    listLayout->addLayout(controls);

    auto* split = new QSplitter(Qt::Vertical, this);
    split->addWidget(listPane);
    split->addWidget(changes);
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 1);
    split->setChildrenCollapsible(false);
    layout->addWidget(split, 1);

    status_ = new QLabel(this);
    status_->setWordWrap(true);
    layout->addWidget(status_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    start_ = buttons->addButton(tr("Start rebase"), QDialogButtonBox::AcceptRole);
    applyRole(start_, QStringLiteral("primary"));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    connect(up_, &QPushButton::clicked, this, [this] { move(-1); });
    connect(down_, &QPushButton::clicked, this, [this] { move(1); });
    connect(list_, &QTreeWidget::itemSelectionChanged, this, &RebaseDialog::revalidate);
    connect(list_, &QTreeWidget::currentItemChanged, this, &RebaseDialog::showCurrentChanges);
    // Double-clicking a subject is how a message is edited everywhere else.
    connect(list_, &QTreeWidget::itemDoubleClicked, this, [this] { reword(); });

    connect(files_, &ChangedFilesList::fileSelected, this, [this](const QString& path) {
        if (!shownOid_.isEmpty() && !path.isEmpty()) {
            session_.requestCommitPeek(shownOid_, path);
        }
    });
    connect(&session_, &session::RepoSession::commitPeekReady, this,
            [this](const QString& oid, session::DetailPtr detail) {
                if (oid != shownOid_ || !detail) {
                    return;
                }
                QString message = QString::fromStdString(detail->summary);
                if (!detail->body.empty()) {
                    message += QStringLiteral("\n\n") + QString::fromStdString(detail->body);
                }
                messages_.insert(oid, message);
                files_->setDetail(detail);
                if (detail->files.empty()) {
                    diff_->clearDiff();
                    diff_->setPlaceholder(tr("This commit changes no files"));
                } else {
                    files_->selectPath(QString::fromStdString(detail->files.front().path));
                }
            });
    connect(&session_, &session::RepoSession::commitPeekDiffReady, this,
            [this](const QString& oid, const QString&, session::FileDiffPtr diff) {
                if (oid == shownOid_) {
                    diff_->setDiff(std::move(diff));
                }
            });

    connect(&session_, &session::RepoSession::commitsSinceReady, this,
            [this](const QString& base, const QStringList& oids, const QStringList& subjects) {
                if (base == baseOid_) {
                    setCommits(oids, subjects);
                }
            });
    session_.requestCommitsSince(baseOid_);
    revalidate();
}

bool RebaseDialog::eventFilter(QObject* watched, QEvent* event) {
    if (watched == list_ && event->type() == QEvent::KeyPress) {
        const auto* key = static_cast<QKeyEvent*>(event);
        if ((key->modifiers() & ~Qt::KeypadModifier) == Qt::NoModifier) {
            for (const ActionChoice& choice : kChoices) {
                if (key->key() == QKeySequence(QString::fromLatin1(choice.key))[0].key()) {
                    if (choice.action == git::RebaseAction::Reword) {
                        reword();
                    } else {
                        applyActionToSelection(choice.action);
                    }
                    return true;
                }
            }
        }
    }
    return QDialog::eventFilter(watched, event);
}

void RebaseDialog::setCommits(const QStringList& oids, const QStringList& subjects) {
    list_->clear();
    const QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);

    for (int i = 0; i < oids.size(); ++i) {
        auto* item = new QTreeWidgetItem(list_);
        item->setData(0, kOidRole, oids.at(i));
        item->setData(0, kSubjectRole, i < subjects.size() ? subjects.at(i) : QString());
        item->setText(1, oids.at(i).left(8));
        item->setFont(1, mono);
        item->setText(2, i < subjects.size() ? subjects.at(i) : QString());
        setAction(item, git::RebaseAction::Pick);
    }
    if (list_->topLevelItemCount() > 0) {
        list_->setCurrentItem(list_->topLevelItem(0));
    }
    revalidate();
}

void RebaseDialog::setAction(QTreeWidgetItem* item, git::RebaseAction action) {
    item->setData(0, kActionRole, static_cast<int>(action));
    item->setText(0, QCoreApplication::translate("gity::ui::RebaseDialog",
                                                 choiceFor(action).label));
    // The subject shows what the commit will say afterwards: the new message
    // for a reword, the old one otherwise.
    const QString subject = item->data(0, kSubjectRole).toString();
    const QString reworded = item->data(0, kMessageRole).toString().section(QChar('\n'), 0, 0);
    const bool showReworded = action == git::RebaseAction::Reword && !reworded.isEmpty();
    item->setText(2, showReworded ? reworded : subject);
    item->setToolTip(2, showReworded ? tr("Was: %1").arg(subject) : QString());

    const QColor colour = action == git::RebaseAction::Drop ? tokens::textDim : tokens::textDefault;
    for (int column = 1; column < 3; ++column) {
        item->setForeground(column, colour);
    }
    QFont font = item->font(2);
    // A dropped commit is struck through as well as coloured: state must
    // never be carried by colour alone.
    font.setStrikeOut(action == git::RebaseAction::Drop);
    font.setItalic(showReworded);
    item->setFont(2, font);
}

void RebaseDialog::applyActionToSelection(git::RebaseAction action) {
    for (QTreeWidgetItem* item : list_->selectedItems()) {
        setAction(item, action);
    }
    revalidate();
}

void RebaseDialog::reword() {
    QTreeWidgetItem* item = list_->currentItem();
    if (item == nullptr) {
        return;
    }
    const QString oid = item->data(0, kOidRole).toString();
    QString message = item->data(0, kMessageRole).toString();
    if (message.isEmpty()) {
        message = messages_.value(oid, item->data(0, kSubjectRole).toString());
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Reword %1").arg(oid.left(8)));
    dialog.resize(560, 300);
    auto* layout = new QVBoxLayout(&dialog);
    auto* edit = new QPlainTextEdit(message, &dialog);
    edit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    layout->addWidget(edit, 1);
    auto* note = new QLabel(tr("The first line is the summary. The changes stay as they are."),
                            &dialog);
    applyRole(note, QStringLiteral("note"));
    layout->addWidget(note);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dialog);
    QPushButton* ok = buttons->addButton(tr("Reword"), QDialogButtonBox::AcceptRole);
    applyRole(ok, QStringLiteral("primary"));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(edit, &QPlainTextEdit::textChanged, &dialog,
            [edit, ok] { ok->setEnabled(!edit->toPlainText().trimmed().isEmpty()); });
    edit->setFocus();
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const QString typed = edit->toPlainText().trimmed();
    // Unchanged is not a reword: nothing to amend.
    if (typed == messages_.value(oid).trimmed()) {
        item->setData(0, kMessageRole, QString());
        setAction(item, git::RebaseAction::Pick);
    } else {
        item->setData(0, kMessageRole, typed);
        setAction(item, git::RebaseAction::Reword);
    }
    revalidate();
}

void RebaseDialog::showCurrentChanges() {
    QTreeWidgetItem* item = list_->currentItem();
    if (item == nullptr) {
        return;
    }
    shownOid_ = item->data(0, kOidRole).toString();
    changesTitle_->setText(tr("Changes in %1 — %2")
                               .arg(shownOid_.left(8), item->data(0, kSubjectRole).toString()));
    files_->setFiles({});
    diff_->clearDiff();
    diff_->setPlaceholder(tr("Loading…"));
    session_.requestCommitPeek(shownOid_);
}

void RebaseDialog::move(int delta) {
    const QList<QTreeWidgetItem*> selected = list_->selectedItems();
    if (selected.size() != 1) {
        return;
    }
    const int from = list_->indexOfTopLevelItem(selected.first());
    const int to = from + delta;
    if (to < 0 || to >= list_->topLevelItemCount()) {
        return;
    }
    QTreeWidgetItem* item = list_->takeTopLevelItem(from);
    list_->insertTopLevelItem(to, item);
    list_->setCurrentItem(item);
    revalidate();
}

std::vector<git::RebaseStep> RebaseDialog::steps() const {
    std::vector<git::RebaseStep> result;
    for (int i = 0; i < list_->topLevelItemCount(); ++i) {
        QTreeWidgetItem* item = list_->topLevelItem(i);
        git::RebaseStep step;
        step.action = static_cast<git::RebaseAction>(item->data(0, kActionRole).toInt());
        step.oid = item->data(0, kOidRole).toString().toStdString();
        step.subject = item->data(0, kSubjectRole).toString().toStdString();
        step.message = item->data(0, kMessageRole).toString().toStdString();
        result.push_back(std::move(step));
    }
    return result;
}

void RebaseDialog::revalidate() {
    const bool one = list_->selectedItems().size() == 1;
    up_->setEnabled(one);
    down_->setEnabled(one);

    const auto problem = git::validateTodo(steps());
    QString message;
    switch (problem) {
    case git::TodoProblem::Empty:
        message = tr("There is nothing above this commit to rebase.");
        break;
    case git::TodoProblem::EverythingDropped:
        message = tr("Every commit is dropped, which discards all of this work. If that is "
                     "what you want, reset the branch instead — it says so more plainly.");
        break;
    case git::TodoProblem::LeadingFold:
        message = tr("The oldest kept commit folds into one that is not part of this rebase. "
                     "Keep it, or start the rebase from further back.");
        break;
    case git::TodoProblem::None:
        break;
    }

    status_->setText(message);
    applyRole(status_, QStringLiteral("warn"));
    start_->setEnabled(problem == git::TodoProblem::None);
}

} // namespace gity::ui
