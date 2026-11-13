// The header over the history: what is listed, and the filters over it — by
// message, author and date.
//
// It sits over the graph rather than in the application toolbar, where it
// stayed on screen over Local Changes, which it does not search (2026-09 UI
// review).
#pragma once

#include "core/git/HistoryStream.h"

#include <QList>
#include <QPair>
#include <QString>
#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QTimer;
class QToolButton;

namespace gity::ui {

class HistoryFilterBar : public QWidget {
    Q_OBJECT

public:
    explicit HistoryFilterBar(QWidget* parent = nullptr);

    /// The filter the controls describe.
    [[nodiscard]] gity::git::CommitFilter filter() const;
    /// Puts `filter` into the controls, without filterChanged.
    void setFilter(const gity::git::CommitFilter& filter);
    /// Says on screen whether the list below is filtered, and what that means.
    void showState(bool filtered);
    /// Narrows the history to the commits that changed `path`, a file or a
    /// folder; empty shows every file again. Sends filterChanged at once.
    void showPath(const QString& path);

    /// The authors offered, most commits first, with how many each made.
    /// What has been typed is kept.
    void setContributors(const QList<QPair<QString, int>>& contributors);

    /// How long typing waits before filterChanged: each change is a full
    /// history walk, and on a 13k-commit project walking on every character
    /// would spend the whole time cancelling itself.
    static constexpr int kDebounceMs = 250;

signals:
    /// The controls changed. Debounced while typing; at once for a date.
    void filterChanged();

private:
    void changedNow();
    void chooseDateRange();
    void refreshDateLabel();

    QLabel* title_ = nullptr;
    QLabel* note_ = nullptr;
    /// The file or folder the history is narrowed to; clicking it lets go.
    QToolButton* pathChip_ = nullptr;
    QString path_;
    QComboBox* author_ = nullptr;
    QToolButton* date_ = nullptr;
    QLineEdit* text_ = nullptr;
    QTimer* debounce_ = nullptr;
    /// Seconds since the epoch; 0 for an open end.
    qint64 since_ = 0;
    qint64 until_ = 0;
};

} // namespace gity::ui
