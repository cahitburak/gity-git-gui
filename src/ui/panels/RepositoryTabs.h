// One tab per open repository.
//
// Every repository gets a tab the moment it is opened — the first one too — so
// the window always says which repository it is showing and a second one is
// an addition rather than a surprise. A submodule opened from the sidebar gets
// a tab of its own, labelled with the repository it belongs to: it is a
// repository in its own right, and keeping it open beside its parent is most
// of why you opened it.
#pragma once

#include <QStringList>
#include <QVariantList>
#include <QWidget>

class QTabBar;

namespace gity::ui {

class RepositoryTabs : public QWidget {
    Q_OBJECT

public:
    explicit RepositoryTabs(QWidget* parent = nullptr);

    /// Adds a tab for `path` if absent, and makes it current. `parentName`
    /// names the repository a submodule belongs to; empty for one opened on
    /// its own.
    void open(const QString& path, const QString& parentName = {});

    /// Makes `path` current if it has a tab. Returns false when it has none,
    /// which is how the caller knows it is a repository not yet open.
    [[nodiscard]] bool activate(const QString& path);

    /// Removes `path`'s tab — a repository that failed to open. Unlike the
    /// close button this may remove the last tab, leaving none.
    void close(const QString& path);

    /// Whether `path` has a tab, without making it current.
    [[nodiscard]] bool contains(const QString& path) const;
    /// Moves `from`'s tab to `to`: a repository opened through a subfolder or
    /// a symlink, once it is known where its working copy really is.
    void rekey(const QString& from, const QString& to);

    [[nodiscard]] QString currentPath() const;
    /// The repository the current tab is a submodule of, by name. Empty for a
    /// repository opened on its own.
    [[nodiscard]] QString currentParentName() const;
    [[nodiscard]] int count() const;
    /// Every open tab's path, in order.
    [[nodiscard]] QStringList paths() const;

    /// The open tabs, in order, as plain data for saving between sessions.
    [[nodiscard]] QVariantList state() const;
    [[nodiscard]] int currentIndex() const;

    /// One spelling for a repository path, so the same directory written two
    /// ways is recognised as the same repository.
    ///
    /// Public because this comparison is not the tabs' alone: anything holding
    /// a repository path has to make it, and doing it by hand is what broke
    /// the tabs.
    [[nodiscard]] static QString canonical(const QString& path);

signals:
    /// A different tab became current and its repository should be shown.
    void repositoryActivated(const QString& path);
    /// The set, its order or the current tab changed — worth saving.
    void tabsChanged();
    /// A tab was closed; whatever was kept for its repository can go.
    void repositoryClosed(const QString& path);

private:
    [[nodiscard]] int indexOf(const QString& path) const;
    [[nodiscard]] QString pathAt(int index) const;
    /// The tab's name and tooltip, for its path and parent.
    void describe(int index, const QString& path, const QString& parentName);
    void removeAt(int index);
    /// The last tab cannot be closed: the window would be left showing a
    /// repository with no tab to say which.
    void updateClosable();

    QTabBar* bar_ = nullptr;
    /// Guards against re-entering the host while it is switching repositories.
    bool suppress_ = false;
};

} // namespace gity::ui
