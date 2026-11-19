// The conflict editor's own window: every conflicted file in a list beside the
// editor, so a merge can be worked through file by file.
//
// A window of its own, not a pane of the main one: three columns of code need
// the width of a screen, and the history and file lists beside them would
// take it. Choices made in a file and not yet written are kept, per
// repository and file, while another file is shown or the window is closed.
// When nothing is left conflicted, it says so and offers to continue the
// operation.
#pragma once

#include "ConflictEditor.h"
#include "core/git/OperationState.h"
#include "session/SessionTypes.h"

#include <QHash>
#include <QList>
#include <QWidget>

#include <optional>

class QLabel;
class QListWidget;
class QPushButton;

namespace gity::ui {

class ConflictWindow : public QWidget {
    Q_OBJECT

public:
    explicit ConflictWindow(QWidget* parent = nullptr);

    /// The repository shown. Choices made in another stay kept for it.
    void setRepository(const QString& workdir, const QString& name);
    void setStatus(session::StatusPtr status);
    void setOperation(git::Operation operation, const QString& noun);

    /// Shows the window on `path`, or on the first conflicted file.
    void openFile(const QString& path = {});
    /// Shows the window on a file of the repository shown whose choices are
    /// not written, or as openFile() does when it has none.
    void openUnsaved();

    void setConflict(const QString& path, session::ConflictPtr conflict, const QString& problem);
    void writeFinished(const QString& path, bool ok, const QString& message);

    /// Choices made and not written, in any repository.
    [[nodiscard]] bool hasUnsaved() const;
    /// The repositories, by workdir, with choices made and not written: the
    /// one shown first.
    [[nodiscard]] QStringList unsavedRepositories() const;
    /// A repository closed: the choices kept for it, written or not, go.
    /// `workdir` may be spelled as its tab spells it.
    void forgetRepository(const QString& workdir);

    [[nodiscard]] ConflictEditor* editor() const noexcept { return editor_; }

signals:
    void conflictRequested(const QString& path);
    void saveRequested(const QString& path, const QByteArray& content, bool markResolved);
    void sideRequested(const QString& path, int side);
    void restartRequested(const QString& path);
    void continueRequested();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    enum class Pending { None, Save, Resolve, Side, Restart };

    /// A write asked for and not yet finished.
    struct Write {
        Pending kind = Pending::None;
        /// For a save: the choices written.
        std::optional<git::MergeResolution> choices;
    };

    /// One file's choices, kept while another is shown.
    struct Kept {
        git::MergeResolution choices;
        /// What was last written from here; none while the file holds what
        /// git wrote.
        std::optional<git::MergeResolution> written;
        /// The choices differ from what the file holds.
        bool unsaved = false;
    };

    void keepChoices();
    /// The file shown in the editor is `path` of this repository.
    [[nodiscard]] bool showing(const QString& path) const;
    /// Choices not written in the repository at `workdir`.
    [[nodiscard]] bool unsavedIn(const QString& workdir) const;
    /// A write that resolves `path` is under way from here.
    [[nodiscard]] bool resolvingHere(const QString& path) const;
    void select(const QString& path);
    void showDone();
    void setNotice(const QString& text, bool error);

    ConflictEditor* editor_ = nullptr;
    QListWidget* files_ = nullptr;
    QLabel* filesHeader_ = nullptr;
    QLabel* notice_ = nullptr;
    QWidget* done_ = nullptr;
    QLabel* doneText_ = nullptr;
    QPushButton* continue_ = nullptr;

    QString workdir_;
    QString name_;
    QString current_;
    QStringList conflicted_;
    git::Operation operation_ = git::Operation::None;
    QString noun_;
    /// Per repository and file, by keyFor(): the writes under way, oldest
    /// first. Per file, so one finishing is told from another asked for
    /// since, and is settled for its own file whichever is shown by then.
    QHash<QString, QList<Write>> writes_;
    /// Per repository, per file: the choices made, so a file shown again
    /// comes back as it was left. The file shown is the editor's; its entry,
    /// if any, is as it was when the file was last left or written.
    QHash<QString, QHash<QString, Kept>> kept_;
};

} // namespace gity::ui
