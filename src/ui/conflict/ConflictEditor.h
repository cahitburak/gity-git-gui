// Resolving one conflicted file.
//
// Three ways to lay it out, chosen at the top and remembered:
//
//  * Three Columns — ours, the result, theirs, side by side. Everything at
//    once, for a wide screen.
//  * Two Columns — ours and theirs only, side by side, each conflict taken
//    from one or the other. For deciding between two versions without the
//    result in between; Edit opens a conflict's result on its own.
//  * Result Below — ours and theirs side by side above, the result across the
//    bottom, at full width for reading and editing.
//
// The columns scroll together and keep each region level across them. A file
// that cannot be merged line by line — binary, or deleted on one side — gets
// a choice of whole versions instead.
#pragma once

#include "ConflictDocument.h"
#include "core/git/OperationState.h"
#include "session/SessionTypes.h"

#include <QWidget>

#include <optional>

class QLabel;
class QPushButton;
class QSplitter;
class QStackedWidget;
class QToolButton;

namespace gity::ui {

class MergePane;

class ConflictEditor : public QWidget {
    Q_OBJECT

public:
    enum class Layout { ThreeColumns = 0, TwoColumns = 1, ResultBelow = 2 };
    /// The whole-file choices; the values are RepoSession::ConflictSide's.
    enum class Side { Ours = 0, Theirs = 1, Delete = 2 };

    explicit ConflictEditor(QWidget* parent = nullptr);

    void setOperation(git::Operation operation);
    void setLayoutChoice(Layout layout);

    [[nodiscard]] Layout layoutChoice() const noexcept { return layout_; }

    /// Shows `file`, starting from `kept` — choices made earlier — when given;
    /// `keptUnsaved` when those were never written.
    void showConflict(session::ConflictPtr file, std::optional<git::MergeResolution> kept = {},
                      bool keptUnsaved = false);
    /// Instead of a file: why it could not be shown, or what to do first.
    void showMessage(const QString& title, const QString& detail);

    [[nodiscard]] QString path() const;
    [[nodiscard]] bool dirty() const;
    /// The choices so far, for keeping while another file is shown.
    [[nodiscard]] std::optional<git::MergeResolution> resolution() const;
    /// The last save was written: what is on screen is what is on disk.
    void markSaved();

signals:
    void saveRequested(const QString& path, const QByteArray& content, bool markResolved);
    void sideRequested(const QString& path, gity::ui::ConflictEditor::Side side);
    void restartRequested(const QString& path);
    void layoutChosen(gity::ui::ConflictEditor::Layout layout);

private:
    struct ColumnView {
        QWidget* box = nullptr;
        QLabel* title = nullptr;
        MergePane* pane = nullptr;
    };

    ColumnView makeColumn(ConflictDocument::Column column);
    void arrange();
    void updateTitles();
    void updateState();
    void goToConflict(int step);
    void setCurrent(std::optional<std::size_t> region);
    void editInDialog(std::size_t region);
    void save(bool markResolved);
    void showWholeFile(const session::ConflictPtr& file);
    [[nodiscard]] QString oursMarker() const;
    [[nodiscard]] QString theirsMarker() const;

    ConflictDocument* document_ = nullptr;
    git::Operation operation_ = git::Operation::None;
    Layout layout_ = Layout::ThreeColumns;
    std::optional<std::size_t> current_;

    QLabel* path_ = nullptr;
    QLabel* status_ = nullptr;
    QToolButton* layoutButtons_[3] = {nullptr, nullptr, nullptr};
    QWidget* toolbar_ = nullptr;
    QPushButton* previous_ = nullptr;
    QPushButton* next_ = nullptr;
    QPushButton* allOurs_ = nullptr;
    QPushButton* allTheirs_ = nullptr;
    QPushButton* reset_ = nullptr;
    QPushButton* restart_ = nullptr;
    QPushButton* save_ = nullptr;
    QPushButton* resolve_ = nullptr;

    QStackedWidget* pages_ = nullptr;
    QWidget* columnsPage_ = nullptr;
    QSplitter* outer_ = nullptr;
    ColumnView ours_;
    ColumnView result_;
    ColumnView theirs_;

    QWidget* wholePage_ = nullptr;
    QLabel* wholeTitle_ = nullptr;
    QLabel* wholeDetail_ = nullptr;
    QPushButton* keepOurs_ = nullptr;
    QPushButton* keepTheirs_ = nullptr;
    QPushButton* deleteFile_ = nullptr;

    QWidget* messagePage_ = nullptr;
    QLabel* messageTitle_ = nullptr;
    QLabel* messageDetail_ = nullptr;
};

} // namespace gity::ui

Q_DECLARE_METATYPE(gity::ui::ConflictEditor::Side)
