// ADR-002 — chrome is built from ordinary widgets.
//
// The graph and diff views are hand-painted because a general-purpose view
// framework stops helping at a million rows. A refs tree has hundreds of rows
// at most, so QTreeWidget is the right tool and native keyboard, accessibility
// and platform behaviour come free. Theming still comes from Theme, not from
// literal colours.
#pragma once

#include "session/RepoSession.h"
#include "ui/panels/RefsSidebarDelegate.h"

#include <QTreeWidget>

namespace gity::ui {

class RefsSidebar : public QTreeWidget {
    Q_OBJECT

public:
    /// The screens the sidebar can switch to. SPEC.md drives navigation from
    /// here rather than from tabs: Working copy is a row in the Workspace
    /// group, not a separate control.
    enum class Screen { History, WorkingCopy };

    explicit RefsSidebar(QWidget* parent = nullptr);

    void setRefs(session::RefSetPtr refs);

    /// Shows only refs whose name contains `text`, case-insensitively. Empty
    /// shows everything. Sections with no surviving rows are hidden rather
    /// than left as empty headings.
    void setFilter(const QString& text);
    void clearRefs();

    /// The heading over Local Changes and All Commits: the repository's own
    /// name.
    void setRepositoryName(const QString& name);

    /// The Local Changes badge. Zero hides it — a "0" badge is noise on a row
    /// that is telling you there is nothing to do.
    void setLocalChangeCount(int changedFiles);

    /// Branches kept at the top, in a Pinned section, as full ref names
    /// ("refs/heads/main", "refs/remotes/origin/release-2"). Remote branches
    /// can be pinned too: a release branch you watch but do not work on is
    /// exactly the one that is hard to find among forty.
    void setPinned(const QStringList& fullNames);

    /// What the host calls a pull request — "Pull Request", "Merge Request" —
    /// for the branch menu's Create item. Empty hides the item: a remote with
    /// no web host (a local path, an unrecognised server) has no page to open.
    void setPullRequestNoun(const QString& noun);

    /// Reflects a screen change that came from somewhere else, e.g. the
    /// elsewhere, so the sidebar never disagrees with the view.
    void selectScreen(Screen screen);

signals:
    /// A ref the user wants to look at. Carries the target commit as hex so
    /// the graph can select it without the UI thread touching libgit2.
    void refActivated(const QString& refName, const QString& targetOid);

    void screenRequested(Screen screen);

    /// A branch the user wants to switch to. Local branches carry their plain
    /// name; a remote branch carries "origin/name", which `git switch` turns
    /// into a local branch tracking it.
    void checkoutRequested(const QString& ref);
    void deleteBranchRequested(const QString& name, bool isCurrent);
    /// Pin or unpin a branch, by full ref name. The host stores the list and
    /// hands it back through setPinned.
    void pinRequested(const QString& fullName, bool pin);
    /// Open the host's page for a new pull request from this remote branch
    /// ("origin/feature/x") — a local branch's upstream, or a remote branch.
    void pullRequestRequested(const QString& remoteBranch);
    /// Verbs that take a branch as their target. `oid` is that branch's tip,
    /// for the ones that need a commit rather than a name.
    void newBranchFromRequested(const QString& base);
    void mergeRequested(const QString& ref);
    void rebaseOntoRequested(const QString& ref);
    void interactiveRebaseRequested(const QString& oid, const QString& label);
    void pushRefRequested(const QString& branch);
    void pullRequested();
    void deleteTagRequested(const QString& name);
    void pushTagRequested(const QString& name);
    void checkoutRefRequested(const QString& ref);
    void stashApplyRequested(const QString& ref, bool pop);
    void stashDropRequested(const QString& ref);

    /// Two refs selected together: show what differs from the first chosen
    /// to the second. Names are for display; the oids are what is compared.
    void compareRequested(const QString& fromName, const QString& fromOid,
                          const QString& toName, const QString& toOid);
    /// More than two refs selected, which has no single difference to show.
    void compareTooMany(int count);

    /// A submodule the user wants to look at as a repository in its own right.
    /// Carries the path relative to the parent's working directory.
    void submoduleActivated(const QString& path);

protected:
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    [[nodiscard]] bool matches(const std::string& name) const;
    /// A stable identity for a row, so a rebuild can find it again.
    [[nodiscard]] static QString keyOf(const QTreeWidgetItem* item);
    /// Selected branches and tags, in the order they were picked.
    [[nodiscard]] QList<QTreeWidgetItem*> selectedRefs() const;
    QTreeWidgetItem* addSection(const QString& title);
    void addWorkspaceGroup();
    QTreeWidgetItem* addWorkspaceRow(QTreeWidgetItem* section, const QString& glyph,
                                     const QString& label, Screen screen, int badge,
                                     SidebarTone tone);
    QTreeWidgetItem* addEntry(QTreeWidgetItem* parent, const git::RefEntry& entry,
                              const QString& glyph, const QString& label);
    void addEntries(QTreeWidgetItem* section, const std::vector<git::RefEntry>& entries,
                    const QString& glyph);
    void addRemoteGroups(QTreeWidgetItem* section, const std::vector<git::RefEntry>& entries);
    void addPinned();
    void addSubmodules(QTreeWidgetItem* section,
                       const std::vector<git::SubmoduleEntry>& submodules);

    session::RefSetPtr refs_;
    int changedFiles_ = 0;
    QString repositoryName_;
    Screen screen_ = Screen::History;
    /// True while this widget is acting on its own click; see selectScreen.
    bool handlingOwnClick_ = false;
    QString filter_;
    QStringList pinned_;
    QString pullRequestNoun_;
    /// Row keys of the selection, oldest pick first.
    QStringList selectionOrder_;
};

} // namespace gity::ui
