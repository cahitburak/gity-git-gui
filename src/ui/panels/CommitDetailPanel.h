// The commit detail panel — ADR-002 chrome, ordinary widgets.
#pragma once

#include "session/RepoSession.h"

#include <QWidget>

#include <QTreeWidget>

class QLabel;
class QPushButton;

namespace gity::ui {

/// The files a commit changed, one row each: status letter, then path.
///
/// Shown twice under the graph — in the Commit tab beside the message, and in
/// the Changes tab beside the diff — so it is its own widget rather than part
/// of either.
class ChangedFilesList : public QTreeWidget {
    Q_OBJECT

public:
    explicit ChangedFilesList(QWidget* parent = nullptr);

    /// Lists `detail`'s files and selects the first, without announcing it:
    /// landing on a file is not the user choosing one.
    void setDetail(const session::DetailPtr& detail);
    /// As setDetail, for any list of files — a comparison's, say.
    void setFiles(const std::vector<git::ChangedFile>& files);
    /// Selects `path` as though the user had, so its diff is requested.
    void selectPath(const QString& path);

signals:
    /// The user picked a file — by click or by keyboard.
    void fileSelected(const QString& path);
};

class CommitDetailPanel : public QWidget {
    Q_OBJECT

public:
    explicit CommitDetailPanel(QWidget* parent = nullptr);

    void setDetail(session::DetailPtr detail);
    void clearDetail();

    /// Shown between selecting a row and its diff arriving, so the panel does
    /// not appear to still describe the previously selected commit.
    void setPending(const QString& shortId, const QString& summary);

    /// Shows two refs side by side instead of one commit: how they relate,
    /// what each has that the other lacks, and the files that differ.
    void setComparison(const session::ComparisonPtr& comparison, const QString& fromName,
                       const QString& toName);
    /// Shown while a comparison loads.
    void setPendingComparison(const QString& fromName, const QString& toName);
    /// Said instead of a comparison when more than two refs are selected.
    void setCannotCompare(int selected);

signals:
    /// The user asked for the comparison the other way round.
    void swapRequested();
    /// A file in the commit's list was clicked.
    void fileActivated(const QString& path);

private:
    void rebuild();

    QLabel* summary_ = nullptr;
    QLabel* meta_ = nullptr;
    QLabel* body_ = nullptr;
    QLabel* fileHeading_ = nullptr;
    QPushButton* swap_ = nullptr;
    ChangedFilesList* files_ = nullptr;

    session::DetailPtr detail_;
};

} // namespace gity::ui
