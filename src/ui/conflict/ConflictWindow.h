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
#include <QSet>
#include <QWidget>

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

    void setConflict(const QString& path, session::ConflictPtr conflict, const QString& problem);
    void writeFinished(const QString& path, bool ok, const QString& message);

    /// Choices made and not written, in any repository.
    [[nodiscard]] bool hasUnsaved() const;

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

    void keepChoices();
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
    Pending pending_ = Pending::None;
    /// Per repository, per file: the choices made, so a file shown again
    /// comes back as it was left.
    QHash<QString, QHash<QString, git::MergeResolution>> kept_;
    /// Which of those were never written: repository and path, by keyFor().
    QSet<QString> unsaved_;
};

} // namespace gity::ui
