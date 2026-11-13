// The Blame tab: one file, who last changed each line, and a way back
// through earlier versions of it.
#pragma once

#include "session/SessionTypes.h"

#include <QString>
#include <QWidget>

#include <vector>

class QLabel;
class QToolButton;

namespace gity::ui {

class BlameView;

class BlamePanel : public QWidget {
    Q_OBJECT

public:
    explicit BlamePanel(QWidget* parent = nullptr);

    /// Blames `path` afresh — as of `revision`, a commit id, or with it empty
    /// as the file is on disk — forgetting the way back.
    void start(const QString& path, const QString& revision);
    /// The session's answer. One for a blame no longer wanted is ignored.
    void setBlame(const QString& path, const QString& revision, const session::BlamePtr& blame,
                  const QString& problem);
    /// Back to empty, for another repository.
    void clear();

    [[nodiscard]] bool showing() const noexcept { return !current_.path.isEmpty(); }

signals:
    /// The panel wants `path` blamed as of `revision`; the window asks the
    /// session, and the answer comes back through setBlame.
    void blameRequested(const QString& path, const QString& revision);
    /// A line's commit was clicked.
    void commitActivated(const QString& id);

private:
    struct Place {
        QString path;
        QString revision;
    };

    void go(const Place& place);
    void describe(const QString& state);

    QToolButton* back_ = nullptr;
    QLabel* title_ = nullptr;
    QLabel* meta_ = nullptr;
    BlameView* view_ = nullptr;
    Place current_;
    /// Where "Blame Before This Change" came from, most recent last.
    std::vector<Place> trail_;
};

} // namespace gity::ui
