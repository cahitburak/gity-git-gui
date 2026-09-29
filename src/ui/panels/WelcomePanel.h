// What the window shows with no repository open: the ways in, and the
// repositories opened recently — most of the time, the one wanted is there.
#pragma once

#include <QStringList>
#include <QWidget>

class QListWidget;
class QLabel;

namespace gity::ui {

class WelcomePanel : public QWidget {
    Q_OBJECT

public:
    explicit WelcomePanel(QWidget* parent = nullptr);

    /// Most recent first. Paths no longer on disk are listed but disabled, so
    /// a repository that moved is recognisable rather than silently gone.
    void setRecent(const QStringList& paths);

signals:
    void openRequested();
    void cloneRequested();
    void recentActivated(const QString& path);

private:
    QListWidget* recent_ = nullptr;
    QLabel* recentHeading_ = nullptr;
};

} // namespace gity::ui
