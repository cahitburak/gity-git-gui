#include "CommitDetailPanel.h"

#include "ui/panels/Confirm.h"
#include "ui/theme/Theme.h"

#include <QDateTime>

#include <algorithm>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace gity::ui {
namespace {

QString hexOf(const git_oid& oid, int chars = 8) {
    char buffer[GIT_OID_SHA1_HEXSIZE + 1] = {};
    git_oid_tostr(buffer, sizeof(buffer), &oid);
    return QString::fromLatin1(buffer).left(chars);
}

QString whenText(std::int64_t seconds) {
    return QDateTime::fromSecsSinceEpoch(seconds).toString(QStringLiteral("yyyy-MM-dd hh:mm"));
}

QColor statusColour(git::ChangeStatus status, const Theme& theme) {
    switch (status) {
    case git::ChangeStatus::Added:
        return theme.laneColor(4); // green-ish in both palettes
    case git::ChangeStatus::Deleted:
        return theme.laneColor(3); // red-ish
    case git::ChangeStatus::Renamed:
    case git::ChangeStatus::Copied:
        return theme.laneColor(2);
    default:
        return theme.textMuted;
    }
}

constexpr int kPathRole = Qt::UserRole;

/// Commits listed per side of a comparison before "… and N more".
constexpr std::size_t kShown = 10;

} // namespace

ChangedFilesList::ChangedFilesList(QWidget* parent) : QTreeWidget(parent) {
    setColumnCount(2);
    setHeaderHidden(true);
    setRootIsDecorated(false);
    setUniformRowHeights(true);
    header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    header()->setSectionResizeMode(1, QHeaderView::Stretch);

    // Current item rather than click, so arrow keys walk the files and each
    // one's diff follows — reading a commit file by file is the common case.
    connect(this, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem* current, QTreeWidgetItem*) {
                if (current == nullptr) {
                    return;
                }
                const QString path = current->data(0, kPathRole).toString();
                if (!path.isEmpty()) {
                    emit fileSelected(path);
                }
            });
}

void ChangedFilesList::setDetail(const session::DetailPtr& detail) {
    if (!detail) {
        const QSignalBlocker quiet(this);
        clear();
        return;
    }
    setFiles(detail->files);
}

void ChangedFilesList::setFiles(const std::vector<git::ChangedFile>& files) {
    const QSignalBlocker quiet(this);
    clear();
    const Theme& theme = Theme::current();
    for (const auto& file : files) {
        auto* item = new QTreeWidgetItem(this);
        item->setText(0, QString(QChar(git::statusLetter(file.status))));
        item->setForeground(0, statusColour(file.status, theme));

        QString label = QString::fromStdString(file.path);
        if (!file.oldPath.empty()) {
            label = tr("%1 ← %2").arg(label, QString::fromStdString(file.oldPath));
        }
        item->setText(1, label);
        item->setData(0, kPathRole, QString::fromStdString(file.path));
        if (file.binary) {
            item->setForeground(1, theme.textMuted);
            item->setToolTip(1, tr("Binary file"));
        }
    }
    if (topLevelItemCount() > 0) {
        setCurrentItem(topLevelItem(0));
    }
}

void ChangedFilesList::selectPath(const QString& path) {
    for (int i = 0; i < topLevelItemCount(); ++i) {
        QTreeWidgetItem* item = topLevelItem(i);
        if (item->data(0, kPathRole).toString() == path) {
            if (item == currentItem()) {
                emit fileSelected(path);
            } else {
                setCurrentItem(item);
            }
            scrollToItem(item);
            return;
        }
    }
}

CommitDetailPanel::CommitDetailPanel(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 10, 14, 10);
    layout->setSpacing(6);

    summary_ = new QLabel(this);
    summary_->setWordWrap(true);
    summary_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    QFont summaryFont = summary_->font();
    summaryFont.setBold(true);
    summary_->setFont(summaryFont);

    // Only while comparing: which side is "from" decides whether a line reads
    // as added or removed, and either reading can be the one wanted.
    swap_ = new QPushButton(tr("Swap"), this);
    swap_->setToolTip(tr("Compare the other way round"));
    swap_->setVisible(false);
    connect(swap_, &QPushButton::clicked, this, &CommitDetailPanel::swapRequested);

    auto* titleRow = new QHBoxLayout;
    titleRow->addWidget(summary_, 1);
    titleRow->addWidget(swap_, 0, Qt::AlignTop);
    layout->addLayout(titleRow);

    meta_ = new QLabel(this);
    meta_->setTextFormat(Qt::PlainText);
    meta_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    applyRole(meta_, QStringLiteral("muted"));
    layout->addWidget(meta_);

    body_ = new QLabel(this);
    body_->setWordWrap(true);
    body_->setTextFormat(Qt::PlainText);
    body_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    body_->setVisible(false);
    layout->addWidget(body_);

    fileHeading_ = new QLabel(this);
    applyRole(fileHeading_, QStringLiteral("note"));
    layout->addWidget(fileHeading_);

    files_ = new ChangedFilesList(this);
    layout->addWidget(files_, 1);

    // A click here means "show me this file", and the diff lives in the
    // Changes tab — so the click is handed up rather than acted on.
    connect(files_, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* item, int) {
        const QString path = item->data(0, kPathRole).toString();
        if (!path.isEmpty()) {
            emit fileActivated(path);
        }
    });
    connect(files_, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* item, int) {
        const QString path = item->data(0, kPathRole).toString();
        if (!path.isEmpty()) {
            emit fileActivated(path);
        }
    });

    clearDetail();
}

void CommitDetailPanel::clearDetail() {
    detail_.reset();
    swap_->setVisible(false);
    summary_->setText(tr("No commit selected"));
    meta_->clear();
    body_->clear();
    body_->setVisible(false);
    fileHeading_->clear();
    files_->clear();
}

void CommitDetailPanel::setPending(const QString& shortId, const QString& summary) {
    detail_.reset();
    swap_->setVisible(false);
    summary_->setText(summary);
    meta_->setText(tr("%1 · loading…").arg(shortId));
    body_->clear();
    body_->setVisible(false);
    fileHeading_->clear();
    files_->clear();
}

void CommitDetailPanel::setDetail(session::DetailPtr detail) {
    detail_ = std::move(detail);
    rebuild();
}

void CommitDetailPanel::setPendingComparison(const QString& fromName, const QString& toName) {
    detail_.reset();
    swap_->setVisible(true);
    summary_->setText(tr("Comparing %1 → %2").arg(fromName, toName));
    meta_->setText(tr("Loading…"));
    body_->clear();
    body_->setVisible(false);
    fileHeading_->clear();
    files_->clear();
}

void CommitDetailPanel::setCannotCompare(int selected) {
    detail_.reset();
    swap_->setVisible(false);
    summary_->setText(tr("%1 branches selected").arg(selected));
    meta_->setText(tr("Select exactly two branches or tags to see the difference between "
                      "them."));
    body_->clear();
    body_->setVisible(false);
    fileHeading_->clear();
    files_->clear();
}

void CommitDetailPanel::setComparison(const session::ComparisonPtr& comparison,
                                      const QString& fromName, const QString& toName) {
    detail_.reset();
    if (!comparison) {
        clearDetail();
        return;
    }
    swap_->setVisible(true);
    summary_->setText(tr("Comparing %1 → %2").arg(fromName, toName));

    QStringList lines;
    lines << tr("From\t%1  %2").arg(fromName, hexOf(comparison->from));
    lines << tr("To\t%1  %2").arg(toName, hexOf(comparison->to));
    lines << (comparison->hasMergeBase
                  ? tr("Merge base\t%1").arg(hexOf(comparison->mergeBase))
                  : tr("Merge base\tnone — the histories are unrelated"));
    if (comparison->onlyInTo == 0 && comparison->onlyInFrom == 0) {
        lines << tr("History\tthe same commit");
    } else {
        lines << tr("History\t%1 is %n commit(s) ahead", nullptr,
                    static_cast<int>(comparison->onlyInTo))
                         .arg(toName) +
                     tr(" and %n behind %1", nullptr, static_cast<int>(comparison->onlyInFrom))
                         .arg(fromName);
    }
    meta_->setText(lines.join(QChar('\n')));

    // What each side has that the other lacks — the half of "the difference"
    // a tree diff cannot show: work that cancels out, or merges.
    //
    // Ten a side: the panel does not scroll, the file list below is the part
    // being read, and the graph just above shows every commit anyway.
    const auto listing = [](const QString& heading, std::size_t count,
                            const std::vector<git::ComparedCommit>& commits) {
        QStringList out;
        out << heading;
        const std::size_t shown = std::min(kShown, commits.size());
        for (std::size_t i = 0; i < shown; ++i) {
            out << QStringLiteral("    %1  %2").arg(hexOf(commits[i].oid),
                                                  QString::fromStdString(commits[i].summary));
        }
        if (count > shown) {
            out << tr("    … and %L1 more").arg(count - shown);
        }
        return out.join(QChar('\n'));
    };
    QStringList sections;
    if (comparison->onlyInTo > 0) {
        sections << listing(tr("Only on %1:").arg(toName), comparison->onlyInTo,
                            comparison->onlyInToCommits);
    }
    if (comparison->onlyInFrom > 0) {
        sections << listing(tr("Only on %1:").arg(fromName), comparison->onlyInFrom,
                            comparison->onlyInFromCommits);
    }
    body_->setText(sections.join(QStringLiteral("\n\n")));
    body_->setVisible(!sections.isEmpty());

    files_->setFiles(comparison->files);
    QString heading = comparison->files.empty()
                          ? tr("No files differ")
                          : tr("%Ln file(s) differ", nullptr,
                               static_cast<int>(comparison->files.size()));
    if (comparison->filesTruncated) {
        heading += tr(" · list truncated");
    }
    fileHeading_->setText(heading);
}

void CommitDetailPanel::rebuild() {
    swap_->setVisible(false);
    if (!detail_) {
        clearDetail();
        return;
    }

    summary_->setText(QString::fromStdString(detail_->summary));

    // The Commit tab has the width for the whole record: who, when, which
    // commit, and what it came from. Plain rows in a
    // grid rather than coloured markup, so the text follows the theme through
    // the stylesheet like everything else.
    const auto person = [](const std::string& name, const std::string& email) {
        return QStringLiteral("%1 <%2>").arg(QString::fromStdString(name),
                                             QString::fromStdString(email));
    };
    QStringList lines;
    lines << tr("Author\t%1  ·  %2")
                 .arg(person(detail_->authorName, detail_->authorEmail),
                      whenText(detail_->authorTime));
    if (detail_->committerName != detail_->authorName ||
        detail_->committerEmail != detail_->authorEmail ||
        detail_->committerTime != detail_->authorTime) {
        lines << tr("Committer\t%1  ·  %2")
                     .arg(person(detail_->committerName, detail_->committerEmail),
                          whenText(detail_->committerTime));
    }
    lines << tr("SHA\t%1").arg(hexOf(detail_->oid, GIT_OID_SHA1_HEXSIZE));
    if (detail_->isRoot()) {
        lines << tr("Parents\tnone — the root commit");
    } else {
        QStringList parents;
        for (const git_oid& parent : detail_->parents) {
            parents << hexOf(parent);
        }
        lines << tr("Parents\t%1").arg(parents.join(QStringLiteral(", ")));
    }
    meta_->setText(lines.join(QChar('\n')));

    const QString body = QString::fromStdString(detail_->body);
    body_->setVisible(!body.isEmpty());
    body_->setText(body);

    files_->setDetail(detail_);

    QString heading = tr("%L1 changed").arg(detail_->files.size());
    if (detail_->filesTruncated) {
        heading += tr(" · list truncated");
    }
    fileHeading_->setText(heading);
}

} // namespace gity::ui
