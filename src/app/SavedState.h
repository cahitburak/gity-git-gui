// What the window keeps between runs, in Gity's own settings: the recent
// repositories, the branches pinned in each repository's sidebar, the open
// tabs, and the pane sizes. Nothing here is written into a repository.
//
// Plain lists and arrays, so the file stays readable and editable by hand.
#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

namespace gity::app::saved {

/// How many repositories File ▸ Open Recent and the welcome screen remember.
inline constexpr int kRecentLimit = 12;

/// Repositories opened in their own right, most recent first.
[[nodiscard]] QStringList recentRepositories();
/// Puts `path` first, once, and returns the list as now saved.
QStringList rememberRepository(const QString& path);
void clearRecentRepositories();

/// The branches pinned in `workdir`'s sidebar, as full ref names.
[[nodiscard]] QStringList pinnedBranches(const QString& workdir);
/// Replaces them; an empty list forgets the repository.
void setPinnedBranches(const QString& workdir, const QStringList& pinned);

struct Tab {
    QString path;
    /// The repository a submodule's tab was opened from; empty otherwise.
    QString parent;
};

struct Tabs {
    QList<Tab> tabs;
    int current = 0;
};

[[nodiscard]] Tabs openTabs();
void saveOpenTabs(const Tabs& tabs);

/// A named splitter's state; empty when none was saved.
[[nodiscard]] QByteArray splitterState(const QString& name);
void saveSplitterState(const QString& name, const QByteArray& state);

} // namespace gity::app::saved
