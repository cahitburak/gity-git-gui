#include "ConflictWindow.h"

#include "ui/panels/Confirm.h"

#include <QCloseEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QVBoxLayout>

namespace gity::ui {
namespace {

const char* const kLayoutKey = "conflictEditor/layout";
const char* const kGeometryKey = "conflictEditor/geometry";

/// Kept choices fit a file only while git's versions of it are the ones they
/// were made against.
bool sameRegions(const git::Merge3& a, const git::Merge3& b) {
    if (a.regions.size() != b.regions.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.regions.size(); ++i) {
        const auto& x = a.regions[i];
        const auto& y = b.regions[i];
        if (x.kind != y.kind || x.base != y.base || x.ours != y.ours || x.theirs != y.theirs) {
            return false;
        }
    }
    return true;
}

QString keyFor(const QString& workdir, const QString& path) {
    return workdir + QLatin1Char('\n') + path;
}

} // namespace

ConflictWindow::ConflictWindow(QWidget* parent) : QWidget(parent, Qt::Window) {
    setObjectName(QStringLiteral("conflictWindow"));
    setWindowTitle(tr("Resolve Conflicts"));
    resize(1400, 860);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* split = new QSplitter(Qt::Horizontal, this);
    layout->addWidget(split);

    auto* side = new QWidget(split);
    side->setObjectName(QStringLiteral("conflictFiles"));
    side->setAttribute(Qt::WA_StyledBackground, true);
    auto* sideLayout = new QVBoxLayout(side);
    sideLayout->setContentsMargins(10, 10, 10, 10);
    sideLayout->setSpacing(8);
    filesHeader_ = new QLabel(tr("Conflicted files"), side);
    filesHeader_->setObjectName(QStringLiteral("conflictFilesHeader"));
    files_ = new QListWidget(side);
    files_->setObjectName(QStringLiteral("conflictFileList"));
    notice_ = new QLabel(side);
    notice_->setWordWrap(true);
    notice_->setTextFormat(Qt::PlainText);
    notice_->setVisible(false);

    done_ = new QWidget(side);
    auto* doneLayout = new QVBoxLayout(done_);
    doneLayout->setContentsMargins(0, 0, 0, 0);
    doneText_ = new QLabel(done_);
    doneText_->setWordWrap(true);
    doneText_->setTextFormat(Qt::PlainText);
    continue_ = new QPushButton(tr("Continue"), done_);
    applyRole(continue_, QStringLiteral("primary"));
    doneLayout->addWidget(doneText_);
    doneLayout->addWidget(continue_);
    done_->setVisible(false);

    sideLayout->addWidget(filesHeader_);
    sideLayout->addWidget(files_, 1);
    sideLayout->addWidget(notice_);
    sideLayout->addWidget(done_);

    editor_ = new ConflictEditor(split);
    split->addWidget(side);
    split->addWidget(editor_);
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 1);
    split->setSizes({240, 1160});
    split->setChildrenCollapsible(false);

    QSettings settings;
    // Anything but three columns is two: the result below. An older third
    // layout, ours and theirs alone, was stored as 1 and the result below as
    // 2; both come back as two columns.
    editor_->setLayoutChoice(settings.value(kLayoutKey, 0).toInt() == 0
                                 ? ConflictEditor::Layout::ThreeColumns
                                 : ConflictEditor::Layout::TwoColumns);
    restoreGeometry(settings.value(kGeometryKey).toByteArray());

    connect(editor_, &ConflictEditor::layoutChosen, this, [](ConflictEditor::Layout chosen) {
        QSettings().setValue(kLayoutKey, static_cast<int>(chosen));
    });
    connect(files_, &QListWidget::currentTextChanged, this, [this](const QString& path) {
        if (!path.isEmpty() && path != current_) {
            select(path);
        }
    });
    connect(editor_, &ConflictEditor::saveRequested, this,
            [this](const QString& path, const QByteArray& content, bool markResolved) {
                pending_ = markResolved ? Pending::Resolve : Pending::Save;
                setNotice(markResolved ? tr("Saving and staging %1…").arg(path)
                                       : tr("Saving %1…").arg(path),
                          false);
                emit saveRequested(path, content, markResolved);
            });
    connect(editor_, &ConflictEditor::sideRequested, this,
            [this](const QString& path, ConflictEditor::Side chosen) {
                pending_ = Pending::Side;
                emit sideRequested(path, static_cast<int>(chosen));
            });
    connect(editor_, &ConflictEditor::restartRequested, this, [this](const QString& path) {
        pending_ = Pending::Restart;
        emit restartRequested(path);
    });
    connect(continue_, &QPushButton::clicked, this, &ConflictWindow::continueRequested);
}

void ConflictWindow::setRepository(const QString& workdir, const QString& name) {
    if (workdir == workdir_) {
        return;
    }
    keepChoices();
    workdir_ = workdir;
    name_ = name;
    current_.clear();
    conflicted_.clear();
    files_->clear();
    setNotice({}, false);
    setWindowTitle(name.isEmpty() ? tr("Resolve Conflicts")
                                  : tr("Resolve Conflicts — %1").arg(name));
    editor_->showConflict(nullptr);
}

void ConflictWindow::setOperation(git::Operation operation, const QString& noun) {
    operation_ = operation;
    noun_ = noun;
    editor_->setOperation(operation);
    if (conflicted_.isEmpty()) {
        showDone();
    }
}

void ConflictWindow::setStatus(session::StatusPtr status) {
    QStringList now;
    if (status) {
        for (const auto& entry : status->entries) {
            if (entry.conflicted) {
                now << QString::fromStdString(entry.path);
            }
        }
    }
    conflicted_ = now;
    filesHeader_->setText(now.isEmpty() ? tr("Conflicted files")
                                        : tr("Conflicted files · %1").arg(now.size()));
    {
        const QSignalBlocker quiet(files_);
        files_->clear();
        files_->addItems(now);
        const auto found = files_->findItems(current_, Qt::MatchExactly);
        if (!found.isEmpty()) {
            files_->setCurrentItem(found.front());
        }
    }
    if (now.contains(current_)) {
        return;
    }
    // The file shown is not conflicted any more — resolved here or
    // elsewhere. Its choices are done with; on to the next.
    if (!current_.isEmpty()) {
        kept_[workdir_].remove(current_);
        unsaved_.remove(keyFor(workdir_, current_));
        current_.clear();
    }
    if (now.isEmpty()) {
        showDone();
    } else if (isVisible()) {
        files_->setCurrentRow(0);
    }
}

void ConflictWindow::showDone() {
    if (!conflicted_.isEmpty()) {
        done_->setVisible(false);
        return;
    }
    const bool underway =
        operation_ != git::Operation::None && operation_ != git::Operation::Bisect;
    if (underway) {
        editor_->showMessage(tr("Nothing is conflicted"),
                             tr("Every file is resolved and staged. Continue %1 to finish it — "
                                "or look over the result in Local Changes first.")
                                 .arg(noun_));
        doneText_->setText(tr("All resolved. Continue %1?").arg(noun_));
        continue_->setToolTip(
            tr("Commit the resolution and carry on, as the banner's Continue does."));
    } else {
        editor_->showMessage(tr("Nothing is conflicted"),
                             tr("No file in this repository has a conflict to resolve."));
    }
    done_->setVisible(underway);
}

void ConflictWindow::openFile(const QString& path) {
    if (isMinimized()) {
        showNormal();
    }
    show();
    raise();
    activateWindow();
    const QString wanted = !path.isEmpty() && conflicted_.contains(path) ? path
                           : !current_.isEmpty()                         ? current_
                           : conflicted_.isEmpty()                       ? QString()
                                                                         : conflicted_.front();
    if (wanted.isEmpty()) {
        showDone();
        return;
    }
    if (wanted != current_) {
        select(wanted);
    }
}

void ConflictWindow::select(const QString& path) {
    keepChoices();
    current_ = path;
    done_->setVisible(false);
    {
        const QSignalBlocker quiet(files_);
        const auto found = files_->findItems(path, Qt::MatchExactly);
        if (!found.isEmpty()) {
            files_->setCurrentItem(found.front());
        }
    }
    editor_->showMessage(path, tr("Reading the versions git kept…"));
    emit conflictRequested(path);
}

void ConflictWindow::keepChoices() {
    if (current_.isEmpty() || editor_->path() != current_ || !editor_->dirty()) {
        return;
    }
    if (auto choices = editor_->resolution(); choices) {
        kept_[workdir_].insert(current_, std::move(*choices));
        unsaved_.insert(keyFor(workdir_, current_));
    }
}

void ConflictWindow::setConflict(const QString& path, session::ConflictPtr conflict,
                                 const QString& problem) {
    if (path != current_) {
        return;
    }
    if (!conflict) {
        editor_->showMessage(tr("%1 could not be opened").arg(path), problem);
        return;
    }
    std::optional<git::MergeResolution> kept;
    auto& inRepo = kept_[workdir_];
    if (const auto found = inRepo.constFind(path); found != inRepo.constEnd()) {
        if (sameRegions(found->merge(), conflict->merge)) {
            kept = *found;
        } else {
            // git's versions changed under them — Start Over, or a new
            // merge. They no longer say anything about this file.
            inRepo.remove(path);
            unsaved_.remove(keyFor(workdir_, path));
        }
    }
    editor_->showConflict(std::move(conflict), std::move(kept),
                          unsaved_.contains(keyFor(workdir_, path)));
}

void ConflictWindow::writeFinished(const QString& path, bool ok, const QString& message) {
    const Pending was = pending_;
    pending_ = Pending::None;
    setNotice(message, !ok);
    if (!ok || path != current_) {
        return;
    }
    switch (was) {
    case Pending::Save:
        // Kept, so the file comes back with these choices if shown again:
        // the versions git holds still have every conflict open.
        editor_->markSaved();
        if (auto choices = editor_->resolution(); choices) {
            kept_[workdir_].insert(path, std::move(*choices));
        }
        unsaved_.remove(keyFor(workdir_, path));
        break;
    case Pending::Restart:
        // The same versions, and the choices gone with the file's content.
        kept_[workdir_].remove(path);
        unsaved_.remove(keyFor(workdir_, path));
        editor_->showMessage(path, tr("Reading the versions git kept…"));
        emit conflictRequested(path);
        break;
    case Pending::Resolve:
    case Pending::Side:
        // The status that follows moves on to the next file.
        editor_->markSaved();
        kept_[workdir_].remove(path);
        unsaved_.remove(keyFor(workdir_, path));
        break;
    case Pending::None:
        break;
    }
}

bool ConflictWindow::hasUnsaved() const {
    return editor_->dirty() || !unsaved_.isEmpty();
}

void ConflictWindow::setNotice(const QString& text, bool error) {
    notice_->setText(text);
    notice_->setVisible(!text.isEmpty());
    applyRole(notice_, error ? QStringLiteral("error") : QStringLiteral("muted"));
}

void ConflictWindow::closeEvent(QCloseEvent* event) {
    keepChoices();
    QSettings().setValue(kGeometryKey, saveGeometry());
    QWidget::closeEvent(event);
}

} // namespace gity::ui
