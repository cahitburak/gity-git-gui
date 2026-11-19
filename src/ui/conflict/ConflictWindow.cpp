#include "ConflictWindow.h"

#include "ui/panels/Confirm.h"

#include <QCloseEvent>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QVBoxLayout>

#include <algorithm>

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

QString workdirOf(const QString& key) {
    return key.section(QLatin1Char('\n'), 0, 0);
}

/// The same directory, however each is spelled: a tab and the session may
/// name a repository differently.
bool sameDirectory(const QString& a, const QString& b) {
    if (QDir::cleanPath(a) == QDir::cleanPath(b)) {
        return true;
    }
    const QString canonical = QFileInfo(a).canonicalFilePath();
    return !canonical.isEmpty() && canonical == QFileInfo(b).canonicalFilePath();
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
                Write write;
                write.kind = markResolved ? Pending::Resolve : Pending::Save;
                write.choices = editor_->resolution();
                writes_[keyFor(workdir_, path)].append(std::move(write));
                setNotice(markResolved ? tr("Saving and staging %1…").arg(path)
                                       : tr("Saving %1…").arg(path),
                          false);
                emit saveRequested(path, content, markResolved);
            });
    connect(editor_, &ConflictEditor::sideRequested, this,
            [this](const QString& path, ConflictEditor::Side chosen) {
                writes_[keyFor(workdir_, path)].append(Write{Pending::Side, std::nullopt});
                emit sideRequested(path, static_cast<int>(chosen));
            });
    connect(editor_, &ConflictEditor::restartRequested, this, [this](const QString& path) {
        writes_[keyFor(workdir_, path)].append(Write{Pending::Restart, std::nullopt});
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

    // Files not conflicted any more — resolved here or elsewhere — are done
    // with, whichever is shown: their choices go. Said, not dropped quietly,
    // when they were never written and the editor did not resolve the file.
    QStringList lost;
    const bool shownGoes = !current_.isEmpty() && !now.contains(current_);
    if (shownGoes && showing(current_) && editor_->dirty() && !resolvingHere(current_)) {
        lost << current_;
    }
    if (status && !workdir_.isEmpty()) {
        if (const auto repo = kept_.find(workdir_); repo != kept_.end()) {
            for (auto it = repo->begin(); it != repo->end();) {
                if (now.contains(it.key())) {
                    ++it;
                    continue;
                }
                // The shown file's entry is older than the editor, which
                // answered above.
                if (it->unsaved && !showing(it.key()) && !resolvingHere(it.key())) {
                    lost << it.key();
                }
                it = repo->erase(it);
            }
        }
    }
    if (lost.size() == 1) {
        setNotice(tr("%1 was resolved outside the conflict editor; the choices made here and "
                     "not saved were set aside.")
                      .arg(lost.front()),
                  false);
    } else if (!lost.isEmpty()) {
        setNotice(tr("%1 were resolved outside the conflict editor; the choices made here and "
                     "not saved were set aside.")
                      .arg(lost.join(QStringLiteral(", "))),
                  false);
    }
    if (now.contains(current_)) {
        return;
    }
    // The file shown went with them: on to the next.
    if (!current_.isEmpty()) {
        kept_[workdir_].remove(current_);
        current_.clear();
        // Nor left in the editor, where it would still count as unsaved.
        editor_->showConflict(nullptr);
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

void ConflictWindow::openUnsaved() {
    if (showing(current_) && editor_->dirty()) {
        openFile(current_);
        return;
    }
    if (const auto repo = kept_.constFind(workdir_); repo != kept_.constEnd()) {
        // In the list's order, so it is the first one there.
        for (const QString& path : std::as_const(conflicted_)) {
            if (const auto found = repo->constFind(path);
                found != repo->constEnd() && found->unsaved) {
                openFile(path);
                return;
            }
        }
    }
    openFile();
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
    if (!showing(current_)) {
        return;
    }
    auto choices = editor_->resolution(); // an edit still open is finished first
    if (!choices) {
        return;
    }
    auto& inRepo = kept_[workdir_];
    const bool unsaved = editor_->dirty();
    std::optional<git::MergeResolution> written;
    if (const auto found = inRepo.constFind(current_); found != inRepo.constEnd()) {
        written = found->written;
    }
    if (!unsaved && !written) {
        // As git left it: nothing to bring back.
        inRepo.remove(current_);
        return;
    }
    inRepo.insert(current_, Kept{std::move(*choices), std::move(written), unsaved});
}

bool ConflictWindow::showing(const QString& path) const {
    return !path.isEmpty() && path == current_ && editor_->path() == path;
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
    std::optional<git::MergeResolution> written;
    auto& inRepo = kept_[workdir_];
    if (const auto found = inRepo.constFind(path); found != inRepo.constEnd()) {
        if (sameRegions(found->choices.merge(), conflict->merge)) {
            kept = found->choices;
            written = found->written;
        } else {
            // git's versions changed under them — Start Over, or a new
            // merge. They no longer say anything about this file.
            inRepo.remove(path);
        }
    }
    editor_->showConflict(std::move(conflict), std::move(kept), std::move(written));
}

void ConflictWindow::writeFinished(const QString& path, bool ok, const QString& message) {
    // The oldest write of this file is the one finishing, whichever file is
    // shown now: it is settled for its own file.
    Write write;
    if (const auto found = writes_.find(keyFor(workdir_, path)); found != writes_.end()) {
        if (!found->isEmpty()) {
            write = found->takeFirst();
        }
        if (found->isEmpty()) {
            writes_.erase(found);
        }
    }
    setNotice(message, !ok);
    if (!ok) {
        return;
    }
    auto& inRepo = kept_[workdir_];
    switch (write.kind) {
    case Pending::Save:
        if (!write.choices) {
            break;
        }
        // Kept, so the file comes back with these choices if shown again:
        // the versions git holds still have every conflict open.
        if (showing(path)) {
            // Changed since they were sent, the editor still says so.
            editor_->markSaved(*write.choices);
            inRepo.insert(path, Kept{*write.choices, write.choices, false});
        } else if (const auto found = inRepo.find(path); found != inRepo.end()) {
            // Left while it was written: what is kept is unsaved only where
            // it was changed after.
            found->written = write.choices;
            found->unsaved = found->choices.text() != write.choices->text();
        } else {
            inRepo.insert(path, Kept{*write.choices, write.choices, false});
        }
        break;
    case Pending::Restart:
        // The same versions, and the choices gone with the file's content —
        // and not brought back by leaving the file while it was written.
        inRepo.remove(path);
        if (path == current_) {
            editor_->showMessage(path, tr("Reading the versions git kept…"));
            emit conflictRequested(path);
        }
        break;
    case Pending::Resolve:
        // The status that follows moves on to the next file; until then
        // what is shown is what was written.
        inRepo.remove(path);
        if (showing(path) && write.choices) {
            editor_->markSaved(*write.choices);
        }
        break;
    case Pending::Side:
        inRepo.remove(path);
        break;
    case Pending::None:
        break;
    }
}

bool ConflictWindow::unsavedIn(const QString& workdir) const {
    if (workdir == workdir_ && showing(current_) && editor_->dirty()) {
        return true;
    }
    const auto repo = kept_.constFind(workdir);
    if (repo == kept_.constEnd()) {
        return false;
    }
    for (auto it = repo->constBegin(); it != repo->constEnd(); ++it) {
        // The file shown answers for itself, above: its entry is older.
        if (it->unsaved && !(workdir == workdir_ && showing(it.key()))) {
            return true;
        }
    }
    return false;
}

bool ConflictWindow::resolvingHere(const QString& path) const {
    const auto found = writes_.constFind(keyFor(workdir_, path));
    if (found == writes_.constEnd()) {
        return false;
    }
    return std::any_of(found->cbegin(), found->cend(), [](const Write& write) {
        return write.kind == Pending::Resolve || write.kind == Pending::Side;
    });
}

bool ConflictWindow::hasUnsaved() const {
    return !unsavedRepositories().isEmpty();
}

QStringList ConflictWindow::unsavedRepositories() const {
    QStringList repositories;
    if (!workdir_.isEmpty() && unsavedIn(workdir_)) {
        repositories << workdir_;
    }
    for (auto it = kept_.constBegin(); it != kept_.constEnd(); ++it) {
        if (it.key() != workdir_ && unsavedIn(it.key())) {
            repositories << it.key();
        }
    }
    return repositories;
}

void ConflictWindow::forgetRepository(const QString& workdir) {
    for (auto it = kept_.begin(); it != kept_.end();) {
        it = sameDirectory(it.key(), workdir) ? kept_.erase(it) : std::next(it);
    }
    for (auto it = writes_.begin(); it != writes_.end();) {
        it = sameDirectory(workdirOf(it.key()), workdir) ? writes_.erase(it) : std::next(it);
    }
    if (!workdir_.isEmpty() && sameDirectory(workdir_, workdir)) {
        // Nor kept again on the way out of it.
        current_.clear();
        setRepository({}, {});
    }
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
