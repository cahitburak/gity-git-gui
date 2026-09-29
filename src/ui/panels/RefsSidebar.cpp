#include "RefsSidebar.h"

#include "ui/panels/Confirm.h"
#include "ui/panels/RefsSidebarDelegate.h"
#include "ui/theme/Theme.h"
#include "ui/theme/ToolbarIcons.h"
#include "ui/theme/Tokens.h"

#include <QApplication>
#include <QClipboard>
#include <QFont>
#include <algorithm>
#include <QContextMenuEvent>
#include <QMenu>
#include <QScopeGuard>
#include <QHeaderView>
#include <QHash>
#include <QMap>
#include <QTreeWidgetItemIterator>

namespace gity::ui {
namespace {

constexpr int kOidRole = Qt::UserRole + 1;
constexpr int kNameRole = Qt::UserRole + 2;
constexpr int kScreenRole = Qt::UserRole + 3;
constexpr int kKindRole = Qt::UserRole + 4;   ///< git::RefKind, for the menu.
constexpr int kIsHeadRole = Qt::UserRole + 5;
constexpr int kSubmoduleRole = Qt::UserRole + 6;
/// Marks the one row that *is* a screen — Local Changes, All Commits — as
/// opposed to rows that merely lead to one, like Detached HEAD.
constexpr int kDestinationRole = Qt::UserRole + 7;
constexpr int kFullNameRole = Qt::UserRole + 8; ///< "refs/heads/main", for pinning.
/// A row in the Pinned section — the same ref also has its usual row, and the
/// two must not be taken for one another when a rebuild restores selection.
constexpr int kPinnedRowRole = Qt::UserRole + 9;
constexpr int kUpstreamRole = Qt::UserRole + 32; ///< "origin/x" a local branch tracks.
// The delegate's roles (SidebarRole) start at Qt::UserRole + 10. These stay
// below it, or well above it: +10 once landed on GlyphRole, and every branch
// with an upstream drew a scrap of the upstream's name instead of its icon.
static_assert(kPinnedRowRole < GlyphRole && kUpstreamRole > TagRole,
              "sidebar item roles must not collide with the delegate's");

QString oidToHex(const git_oid& oid) {
    char buffer[GIT_OID_SHA1_HEXSIZE + 1] = {};
    git_oid_tostr(buffer, sizeof(buffer), &oid);
    return QString::fromLatin1(buffer);
}

/// "↑2 ↓1" — ahead of and behind the upstream. Empty when level, because a
/// row that says nothing is easier to scan past than one that says "0 0".
QString trackingSuffix(const git::RefEntry& entry) {
    if (!entry.hasUpstream || (entry.ahead == 0 && entry.behind == 0)) {
        return {};
    }
    QString suffix;
    if (entry.ahead > 0) {
        suffix += QStringLiteral("↑%1").arg(entry.ahead);
    }
    if (entry.behind > 0) {
        if (!suffix.isEmpty()) {
            suffix += QChar(' ');
        }
        suffix += QStringLiteral("↓%1").arg(entry.behind);
    }
    return suffix;
}

} // namespace

RefsSidebar::RefsSidebar(QWidget* parent) : QTreeWidget(parent) {
    // One column: the delegate lays out glyph, label and badge itself, which
    // is the only way to get the badge as a chip and the 2px selection marker.
    setColumnCount(1);
    setHeaderHidden(true);
    setRootIsDecorated(false);
    setIndentation(16);
    setUniformRowHeights(true);
    // Extended: Ctrl- or Shift-clicking a second branch compares the two.
    // A plain click still selects one row and navigates.
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true); // so hover reaches the delegate
    setItemDelegate(new RefsSidebarDelegate(this));

    // Styled from gity.qss by name; see "named surfaces" there.
    setObjectName(QStringLiteral("refsSidebar"));

    const auto activate = [this](QTreeWidgetItem* item) {
        if (item == nullptr) {
            return;
        }
        handlingOwnClick_ = true;
        const auto done = qScopeGuard([this] { handlingOwnClick_ = false; });
        const QVariant submodule = item->data(0, kSubmoduleRole);
        if (submodule.isValid()) {
            emit submoduleActivated(submodule.toString());
            return;
        }
        const QVariant screen = item->data(0, kScreenRole);
        if (screen.isValid()) {
            screen_ = static_cast<Screen>(screen.toInt());
            emit screenRequested(screen_);
            return;
        }
        const QString oid = item->data(0, kOidRole).toString();
        if (!oid.isEmpty()) {
            // Selecting a ref means looking at history at that point.
            screen_ = Screen::History;
            emit screenRequested(screen_);
            emit refActivated(item->data(0, kNameRole).toString(), oid);
        }
    };
    // Single click, not double: these are navigation, and a sidebar that needs
    // a double click to change screens feels broken.
    connect(this, &QTreeWidget::itemClicked, this,
            [this, activate](QTreeWidgetItem* item, int) {
                // Two refs selected is a comparison; the click that made it is
                // not also a request to jump to the second one's tip.
                const QList<QTreeWidgetItem*> refs = selectedRefs();
                if (refs.size() >= 2) {
                    handlingOwnClick_ = true;
                    const auto done = qScopeGuard([this] { handlingOwnClick_ = false; });
                    screen_ = Screen::History;
                    emit screenRequested(screen_);
                    if (refs.size() == 2) {
                        emit compareRequested(refs[0]->data(0, kNameRole).toString(),
                                              refs[0]->data(0, kOidRole).toString(),
                                              refs[1]->data(0, kNameRole).toString(),
                                              refs[1]->data(0, kOidRole).toString());
                    } else {
                        emit compareTooMany(static_cast<int>(refs.size()));
                    }
                    return;
                }
                // A Ctrl-click that deselected the second of two leaves one
                // behind; that one is what is now being looked at.
                if (!item->isSelected() && selectedItems().size() == 1) {
                    activate(selectedItems().constFirst());
                    return;
                }
                activate(item);
            });

    // Kept in the order the rows were picked, because a comparison has a
    // direction — from the first branch chosen to the second — and the
    // widget's own selection list is in tree order, not click order.
    connect(this, &QTreeWidget::itemSelectionChanged, this, [this] {
        QStringList now;
        for (QTreeWidgetItem* item : selectedItems()) {
            now << keyOf(item);
        }
        QStringList ordered;
        for (const QString& key : std::as_const(selectionOrder_)) {
            if (now.contains(key)) {
                ordered << key;
            }
        }
        for (const QString& key : std::as_const(now)) {
            if (!ordered.contains(key)) {
                ordered << key;
            }
        }
        selectionOrder_ = ordered;
    });

    // Double-click switches branches. Single click only navigates, because
    // changing what is on disk should take more than the click you were
    // already making to look at something.
    connect(this, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item, int) {
        if (item == nullptr || item->data(0, kIsHeadRole).toBool()) {
            return;
        }
        const QVariant kindValue = item->data(0, kKindRole);
        if (!kindValue.isValid()) {
            return;
        }
        const auto kind = static_cast<git::RefKind>(kindValue.toInt());
        if (kind == git::RefKind::LocalBranch || kind == git::RefKind::RemoteBranch) {
            emit checkoutRequested(item->data(0, kNameRole).toString());
        }
    });
}

void RefsSidebar::contextMenuEvent(QContextMenuEvent* event) {
    QTreeWidgetItem* item = itemAt(event->pos());
    if (item == nullptr) {
        return;
    }
    const QVariant kindValue = item->data(0, kKindRole);
    if (!kindValue.isValid()) {
        return; // a section heading, a remote group, a submodule
    }
    setCurrentItem(item);

    using icons::Glyph;
    // Menu text colour, so an icon sits at the same weight as the word by it.
    const auto glyph = [](Glyph which) { return icons::themed(which); };

    const auto kind = static_cast<git::RefKind>(kindValue.toInt());
    const bool isHead = item->data(0, kIsHeadRole).toBool();
    const QString name = item->data(0, kNameRole).toString();
    if (kind == git::RefKind::Tag) {
        QMenu tagMenu(this);
        QAction* visit = tagMenu.addAction(glyph(Glyph::Checkout), tr("Check out %1").arg(name));
        QAction* pushTag =
            tagMenu.addAction(glyph(Glyph::Push), tr("Push %1").arg(name));
        QAction* copyTag = tagMenu.addAction(glyph(Glyph::Copy), tr("Copy Tag Name"));
        tagMenu.addSeparator();
        QAction* dropTag = tagMenu.addAction(glyph(Glyph::Delete), tr("Delete %1").arg(name));
        tagMenu.setToolTipsVisible(true);
        describeCommand(visit, tr("Look at the tagged commit. Commits made there belong to no "
                                  "branch."),
                        QStringLiteral("switch --detach %1").arg(name));
        describeCommand(pushTag, tr("Tags are not sent by Push; this sends this one."),
                        QStringLiteral("push origin refs/tags/%1").arg(name));
        describeCommand(dropTag, tr("Delete the tag here. A pushed copy stays on the remote."),
                        QStringLiteral("tag --delete %1").arg(name));

        QAction* picked = tagMenu.exec(event->globalPos());
        if (picked == visit) {
            emit checkoutRefRequested(name);
        } else if (picked == pushTag) {
            emit pushTagRequested(name);
        } else if (picked == dropTag) {
            emit deleteTagRequested(name);
        } else if (picked == copyTag) {
            QApplication::clipboard()->setText(name);
        }
        return;
    }
    if (kind == git::RefKind::Stash) {
        QMenu stashMenu(this);
        QAction* pop = stashMenu.addAction(glyph(Glyph::StashPop), tr("Pop — apply and remove"));
        QAction* apply = stashMenu.addAction(glyph(Glyph::StashApply), tr("Apply and keep"));
        stashMenu.addSeparator();
        QAction* drop = stashMenu.addAction(glyph(Glyph::Discard), tr("Drop"));
        stashMenu.setToolTipsVisible(true);
        describeCommand(pop, tr("Bring the changes back and remove the stash."),
                        QStringLiteral("stash pop %1").arg(name));
        describeCommand(apply, tr("Bring the changes back and keep the stash."),
                        QStringLiteral("stash apply %1").arg(name));
        describeCommand(drop, tr("Throw the stash away."), QStringLiteral("stash drop %1").arg(name));

        QAction* picked = stashMenu.exec(event->globalPos());
        if (picked == pop) {
            emit stashApplyRequested(name, true);
        } else if (picked == apply) {
            emit stashApplyRequested(name, false);
        } else if (picked == drop) {
            emit stashDropRequested(name);
        }
        return;
    }
    if (kind != git::RefKind::LocalBranch && kind != git::RefKind::RemoteBranch) {
        return; // tags get their own verbs later
    }

    const QString oid = item->data(0, kOidRole).toString();

    QMenu menu(this);
    QAction* checkout = nullptr;
    if (!isHead) {
        checkout = menu.addAction(glyph(Glyph::Checkout),
                                  kind == git::RefKind::RemoteBranch
                                      ? tr("Check out a local branch tracking %1").arg(name)
                                      : tr("Check out %1").arg(name));
    }

    // Network verbs. Pull only for the branch you are on — pulling into a
    // branch you are not standing on is not what the word means.
    menu.addSeparator();
    QAction* pull = nullptr;
    if (isHead) {
        pull = menu.addAction(glyph(Glyph::Pull), tr("Pull into %1").arg(name));
    }
    QAction* push = nullptr;
    if (kind == git::RefKind::LocalBranch) {
        push = menu.addAction(glyph(Glyph::Push), tr("Push %1").arg(name));
    }
    // From the branch as the remote has it: a local branch's upstream, or the
    // remote branch itself. A branch never pushed has nothing to propose yet.
    const QString remoteBranch = kind == git::RefKind::RemoteBranch
                                     ? name
                                     : item->data(0, kUpstreamRole).toString();
    QAction* pullRequest = nullptr;
    if (!pullRequestNoun_.isEmpty()) {
        pullRequest = menu.addAction(glyph(Glyph::Merge),
                                     remoteBranch.isEmpty()
                                         ? tr("Create %1 — push the branch first")
                                               .arg(pullRequestNoun_)
                                         : tr("Create %1…").arg(pullRequestNoun_));
        pullRequest->setEnabled(!remoteBranch.isEmpty());
    }

    // Verbs that take this branch as their target, named so it is obvious
    // which way round they run.
    menu.addSeparator();
    QAction* branchFrom =
        menu.addAction(glyph(Glyph::NewBranch), tr("New Branch from %1…").arg(name));
    QAction* merge =
        isHead ? nullptr
               : menu.addAction(glyph(Glyph::Merge), tr("Merge %1 into current").arg(name));
    QAction* rebase =
        isHead ? nullptr
               : menu.addAction(glyph(Glyph::Rebase), tr("Rebase current onto %1").arg(name));
    // Not for the branch you are on: onto its own tip there is nothing to
    // replay, and the dialog would open empty.
    QAction* interactive =
        isHead ? nullptr
               : menu.addAction(glyph(Glyph::Rebase),
                                tr("Interactive Rebase onto %1…").arg(name));

    menu.addSeparator();
    QAction* copy = menu.addAction(glyph(Glyph::Copy), tr("Copy Branch Name"));
    const QString fullName = item->data(0, kFullNameRole).toString();
    const bool isPinned = pinned_.contains(fullName);
    QAction* pin = fullName.isEmpty()
                       ? nullptr
                       : menu.addAction(glyph(Glyph::Pin),
                                        isPinned ? tr("Unpin %1").arg(name)
                                                 : tr("Pin %1 to the Top").arg(name));
    QAction* remove = nullptr;
    if (kind == git::RefKind::LocalBranch) {
        remove = menu.addAction(glyph(Glyph::Delete), tr("Delete %1").arg(name));
        // Deleting the branch you are standing on is not something git will
        // do, and offering it only to refuse is worse than not offering it.
        remove->setEnabled(!isHead);
    }
    if (menu.isEmpty()) {
        return;
    }
    menu.setToolTipsVisible(true);
    describeCommand(checkout,
                    kind == git::RefKind::RemoteBranch
                        ? tr("Make a local branch that follows this one, and switch to it.")
                        : tr("Switch the working copy to this branch."),
                    QStringLiteral("switch %1").arg(name));
    describeCommand(pull, tr("Bring this branch up to date with its upstream."),
                    QStringLiteral("pull --ff-only"));
    describeCommand(push, tr("Send this branch to its remote, publishing it if it is new."),
                    QStringLiteral("push <remote> %1").arg(name));
    describeCommand(branchFrom, tr("Start a new branch at this one's tip."),
                    QStringLiteral("switch -c <name> %1").arg(name));
    describeCommand(merge, tr("Bring this branch's work into the current one."),
                    QStringLiteral("merge %1").arg(name));
    describeCommand(rebase, tr("Replay the current branch's commits on top of this one."),
                    QStringLiteral("rebase %1").arg(name));
    describeCommand(interactive, tr("Reorder, squash or drop commits while moving onto this "
                                    "branch."),
                    QStringLiteral("rebase -i %1").arg(name));
    if (pullRequest != nullptr) {
        pullRequest->setToolTip(
            remoteBranch.isEmpty()
                ? tr("%1 is only here. Push it, then propose it.").arg(name)
                : tr("Opens the host's page for a new %1 from %2 into the default branch, "
                     "in your browser.")
                      .arg(pullRequestNoun_.toLower(), remoteBranch.section(QChar('/'), 1)));
    }
    copy->setToolTip(tr("Put \"%1\" on the clipboard.").arg(name));
    if (pin != nullptr) {
        pin->setToolTip(isPinned ? tr("Take it out of the Pinned section.")
                                 : tr("Keep it in a Pinned section at the top of this list."));
    }
    describeCommand(remove, tr("Delete this branch. Its commits stay while another branch or "
                               "the reflog holds them."),
                    QStringLiteral("branch -d %1").arg(name));

    QAction* chosen = menu.exec(event->globalPos());
    if (chosen == nullptr) {
        return;
    }
    if (chosen == checkout) {
        emit checkoutRequested(name);
    } else if (chosen == pull) {
        emit pullRequested();
    } else if (chosen == push) {
        emit pushRefRequested(name);
    } else if (chosen == branchFrom) {
        emit newBranchFromRequested(name);
    } else if (chosen == merge) {
        emit mergeRequested(name);
    } else if (chosen == rebase) {
        emit rebaseOntoRequested(name);
    } else if (chosen == interactive) {
        emit interactiveRebaseRequested(oid, name);
    } else if (chosen == remove) {
        emit deleteBranchRequested(name, isHead);
    } else if (chosen == pin) {
        emit pinRequested(fullName, !isPinned);
    } else if (chosen == copy) {
        QApplication::clipboard()->setText(name);
    } else if (chosen == pullRequest) {
        emit pullRequestRequested(remoteBranch);
    }
}

QTreeWidgetItem* RefsSidebar::addSection(const QString& title) {
    auto* section = new QTreeWidgetItem(this);
    section->setText(0, title);
    section->setFlags(Qt::ItemIsEnabled); // headings are not selectable

    // SPEC.md §0: group headers use the small uppercase section label.
    QFont label = section->font(0);
    label.setCapitalization(QFont::AllUppercase);
    label.setWeight(QFont::DemiBold);
    label.setPointSize(std::max(7, label.pointSize() - 2));
    section->setFont(0, label);
    section->setData(0, ToneRole, static_cast<int>(SidebarTone::Muted));

    section->setExpanded(true);
    return section;
}

QTreeWidgetItem* RefsSidebar::addEntry(QTreeWidgetItem* parent, const git::RefEntry& entry,
                                       const QString& glyph, const QString& label) {
    auto* item = new QTreeWidgetItem(parent);
    item->setText(0, label);
    item->setData(0, kOidRole, oidToHex(entry.target));
    item->setData(0, kNameRole, QString::fromStdString(entry.name));
    item->setData(0, GlyphRole, glyph);
    item->setData(0, CurrentRole, entry.isHead);
    item->setData(0, kKindRole, static_cast<int>(entry.kind));
    item->setData(0, kIsHeadRole, entry.isHead);
    if (!entry.fullName.empty() && (entry.kind == git::RefKind::LocalBranch ||
                                    entry.kind == git::RefKind::RemoteBranch)) {
        item->setData(0, kFullNameRole, QString::fromStdString(entry.fullName));
    }
    if (entry.isDefault) {
        item->setData(0, TagRole, tr("default"));
    }
    if (entry.hasUpstream) {
        item->setData(0, kUpstreamRole, QString::fromStdString(entry.upstream));
    }

    if (entry.kind == git::RefKind::Stash && !entry.message.empty()) {
        item->setToolTip(0, QString::fromStdString(entry.message));
    }

    const QString tracking = trackingSuffix(entry);
    if (!tracking.isEmpty()) {
        item->setData(0, BadgeRole, tracking);
        // Ahead is the count that carries a consequence — work only you have.
        item->setData(0, BadgeToneRole,
                      static_cast<int>(entry.ahead > 0 ? SidebarTone::Warn
                                                       : SidebarTone::Default));
        item->setToolTip(0, tr("%1 ahead, %2 behind %3")
                                .arg(entry.ahead)
                                .arg(entry.behind)
                                .arg(QString::fromStdString(entry.upstream)));
    } else if (entry.upstreamGone) {
        // Deleted on the remote — usually merged there and cleaned up. Worth
        // noticing: pushing it would quietly recreate the branch, and it is
        // often ready to delete here too.
        item->setData(0, GlyphRole, QStringLiteral("\u26A0"));
        item->setData(0, ToneRole, static_cast<int>(SidebarTone::Warn));
        item->setData(0, BadgeRole, tr("gone"));
        item->setData(0, BadgeToneRole, static_cast<int>(SidebarTone::Warn));
        item->setToolTip(0, tr("%1 was deleted on the remote. This branch and its commits are "
                               "still here — delete it if its work is merged, or push it to "
                               "publish it again.")
                                .arg(QString::fromStdString(entry.upstream)));
    } else if (entry.kind == git::RefKind::LocalBranch && !entry.hasUpstream) {
        item->setData(0, BadgeRole, tr("local"));
        item->setToolTip(0, tr("Tracks no upstream branch"));
    }
    if (entry.isDefault) {
        const QString why = entry.kind == git::RefKind::RemoteBranch
                                ? tr("The remote's default branch.")
                                : tr("Follows the remote's default branch.");
        item->setToolTip(0, item->toolTip(0).isEmpty() ? why : why + QChar('\n') + item->toolTip(0));
    }
    if (entry.isHead) {
        const QString why = tr("Checked out — the branch you are on.");
        item->setToolTip(0, item->toolTip(0).isEmpty() ? why : why + QChar('\n') + item->toolTip(0));
    }
    return item;
}

void RefsSidebar::addEntries(QTreeWidgetItem* section, const std::vector<git::RefEntry>& entries,
                             const QString& glyph) {
    for (const auto& entry : entries) {
        if (!matches(entry.name)) {
            continue;
        }
        addEntry(section, entry, glyph, QString::fromStdString(entry.name));
    }
}

void RefsSidebar::addRemoteGroups(QTreeWidgetItem* section,
                                  const std::vector<git::RefEntry>& entries) {
    // Remote branches arrive as "origin/main". Grouping by the part before the
    // first slash puts them under their remote, which is what makes a list of
    // forty branches across three remotes readable.
    QStringList order;
    QMap<QString, std::vector<const git::RefEntry*>> byRemote;
    for (const auto& entry : entries) {
        if (!matches(entry.name)) {
            continue;
        }
        const QString full = QString::fromStdString(entry.name);
        const qsizetype slash = full.indexOf(QChar('/'));
        const QString remote = slash < 0 ? full : full.left(slash);
        if (!byRemote.contains(remote)) {
            order << remote;
        }
        byRemote[remote].push_back(&entry);
    }

    for (const QString& remote : order) {
        auto* remoteItem = new QTreeWidgetItem(section);
        remoteItem->setText(0, remote);
        remoteItem->setData(0, GlyphRole, QStringLiteral("\u2601"));
        remoteItem->setData(0, ToneRole, static_cast<int>(SidebarTone::Muted));
        remoteItem->setData(0, BadgeRole, QString::number(byRemote[remote].size()));
        // Not selectable: a remote is a heading here, and clicking it has no
        // commit to show.
        remoteItem->setFlags(Qt::ItemIsEnabled);

        for (const git::RefEntry* entry : byRemote[remote]) {
            const QString full = QString::fromStdString(entry->name);
            const qsizetype slash = full.indexOf(QChar('/'));
            // The remote's name is the parent row; repeating it on every child
            // is noise.
            addEntry(remoteItem, *entry, QStringLiteral("\u2442"),
                     slash < 0 ? full : full.mid(slash + 1));
        }
        remoteItem->setExpanded(byRemote[remote].size() <= 12);
    }
}

void RefsSidebar::addPinned() {
    if (pinned_.isEmpty() || !refs_) {
        return;
    }
    QTreeWidgetItem* section = nullptr;
    // In the order they were pinned: the list is yours, not alphabetical.
    for (const QString& fullName : std::as_const(pinned_)) {
        const std::string wanted = fullName.toStdString();
        const bool remote = fullName.startsWith(QStringLiteral("refs/remotes/"));
        const auto& entries = remote ? refs_->remoteBranches : refs_->localBranches;
        const auto found = std::find_if(entries.begin(), entries.end(),
                                        [&wanted](const auto& e) { return e.fullName == wanted; });
        // A pinned branch that is gone — deleted, or pruned by a fetch — is
        // left in the list, not shown: it may well come back.
        if (found == entries.end() || !matches(found->name)) {
            continue;
        }
        if (section == nullptr) {
            section = addSection(tr("Pinned"));
        }
        // The full shorthand, "origin/release-2": here, unlike under Remotes,
        // there is no parent row saying which remote it is.
        QTreeWidgetItem* row = addEntry(section, *found, QStringLiteral("\u2442"),
                                        QString::fromStdString(found->name));
        row->setData(0, kPinnedRowRole, true);
    }
}

void RefsSidebar::setPullRequestNoun(const QString& noun) {
    pullRequestNoun_ = noun;
}

void RefsSidebar::setPinned(const QStringList& fullNames) {
    if (fullNames == pinned_) {
        return;
    }
    pinned_ = fullNames;
    setRefs(refs_);
}

void RefsSidebar::addSubmodules(QTreeWidgetItem* section,
                                const std::vector<git::SubmoduleEntry>& submodules) {
    for (const auto& submodule : submodules) {
        if (!matches(submodule.path)) {
            continue;
        }
        auto* item = new QTreeWidgetItem(section);
        item->setText(0, QString::fromStdString(submodule.path));
        item->setData(0, GlyphRole, QStringLiteral("\u229e"));
        item->setData(0, BadgeRole, QString::fromStdString(submodule.recordedShortId));
        // A submodule is a repository, so its row opens it as one. Only when
        // its files are actually on disk — an uninitialised one has nothing to
        // open, and a row that does nothing when clicked is worse than one
        // that says why.
        if (submodule.initialised) {
            item->setData(0, kSubmoduleRole, QString::fromStdString(submodule.path));
        } else {
            item->setFlags(Qt::ItemIsEnabled);
        }

        if (!submodule.initialised) {
            item->setData(0, ToneRole, static_cast<int>(SidebarTone::Muted));
            item->setToolTip(0, tr("Not initialised — its files are not on disk."));
        } else if (submodule.needsAttention()) {
            // The state worth raising a voice about: what is on disk is not
            // what the superproject records, and nothing in the main history
            // shows it.
            item->setData(0, ToneRole, static_cast<int>(SidebarTone::Warn));
            item->setToolTip(0, submodule.movedAway
                                    ? tr("Checked out at a different commit than this "
                                         "repository records.")
                                    : tr("Has uncommitted changes of its own."));
        } else {
            item->setToolTip(0, QString::fromStdString(submodule.url));
        }
    }
}

void RefsSidebar::setLocalChangeCount(int changedFiles) {
    if (changedFiles == changedFiles_) {
        return;
    }
    changedFiles_ = changedFiles;
    setRefs(refs_);
}

void RefsSidebar::setRepositoryName(const QString& name) {
    if (name == repositoryName_) {
        return;
    }
    repositoryName_ = name;
    setRefs(refs_);
}

void RefsSidebar::selectScreen(Screen screen) {
    screen_ = screen;
    // When the sidebar itself was clicked, the user has already chosen a row —
    // a branch, a tag — and the host echoing the screen change back would move
    // the selection off it. Several Workspace rows map to the History screen,
    // so this used to land on "Stashes" whenever a branch was clicked.
    if (handlingOwnClick_) {
        return;
    }
    for (int i = 0; i < topLevelItemCount(); ++i) {
        QTreeWidgetItem* section = topLevelItem(i);
        for (int j = 0; j < section->childCount(); ++j) {
            QTreeWidgetItem* row = section->child(j);
            if (row == nullptr) {
                continue;
            }
            const QVariant value = row->data(0, kScreenRole);
            if (value.isValid() && row->data(0, kDestinationRole).toBool() &&
                static_cast<Screen>(value.toInt()) == screen) {
                setCurrentItem(row);
                return;
            }
        }
    }
}

QTreeWidgetItem* RefsSidebar::addWorkspaceRow(QTreeWidgetItem* section, const QString& glyph,
                                              const QString& label, Screen screen, int badge,
                                              SidebarTone tone) {
    auto* item = new QTreeWidgetItem(section);
    item->setText(0, label);
    item->setData(0, kScreenRole, static_cast<int>(screen));
    item->setData(0, GlyphRole, glyph);
    item->setData(0, ToneRole, static_cast<int>(tone));
    if (badge > 0) {
        // Zero hides the badge: a "0" on a row telling you there is nothing to
        // do is noise.
        item->setData(0, BadgeRole, QString::number(badge));
        item->setData(0, BadgeToneRole, static_cast<int>(tone));
    }
    return item;
}

void RefsSidebar::addWorkspaceGroup() {
    // The repository by name, then the two places you go —
    // what you are changing, and what has been committed.
    auto* section =
        addSection(repositoryName_.isEmpty() ? tr("Repository") : repositoryName_);
    // A name, not a label: shown as the repository spells it, where the other
    // headings are set in capitals.
    // And at full size and in full colour: it titles everything under it.
    QFont name = font();
    name.setWeight(QFont::DemiBold);
    section->setFont(0, name);
    section->setData(0, ToneRole, static_cast<int>(SidebarTone::Default));
    // Detached HEAD earns a row of its own: no branch is bold in the list
    // below, and the absence of a marker is not something anyone notices.
    if (refs_ && refs_->headDetached) {
        auto* row = addWorkspaceRow(section, QStringLiteral("⚑"), tr("Detached HEAD"),
                                    Screen::History, 0, SidebarTone::Warn);
        row->setToolTip(0, tr("HEAD is at %1 and not on any branch. Commits made here are "
                              "reachable only by their id once you check something else out — "
                              "make a branch first if you mean to keep them.")
                                .arg(QString::fromStdString(refs_->headName)));
    }
    addWorkspaceRow(section, QStringLiteral("◧"), tr("Local Changes"), Screen::WorkingCopy,
                    changedFiles_, SidebarTone::Default)
        ->setData(0, kDestinationRole, true);
    addWorkspaceRow(section, QStringLiteral("⎇"), tr("All Commits"), Screen::History, 0,
                    SidebarTone::Default)
        ->setData(0, kDestinationRole, true);
}

void RefsSidebar::setFilter(const QString& text) {
    const QString trimmed = text.trimmed();
    if (trimmed == filter_) {
        return;
    }
    filter_ = trimmed;
    // Rebuilt rather than hiding rows in place: nested remotes and section
    // headings both have to disappear when nothing under them matches, and
    // rebuilding states that once instead of in three places.
    setRefs(refs_);
}

bool RefsSidebar::matches(const std::string& name) const {
    return filter_.isEmpty() ||
           QString::fromStdString(name).contains(filter_, Qt::CaseInsensitive);
}

QList<QTreeWidgetItem*> RefsSidebar::selectedRefs() const {
    // Branches, remote branches and tags: anything with a commit to compare.
    // A stash could be compared too, but it is a working-copy snapshot and
    // reads confusingly as one side of "difference between branches".
    QList<QTreeWidgetItem*> refs;
    const QList<QTreeWidgetItem*> selected = selectedItems();
    for (const QString& key : selectionOrder_) {
        for (QTreeWidgetItem* item : selected) {
            if (keyOf(item) != key) {
                continue;
            }
            const QVariant kind = item->data(0, kKindRole);
            if (kind.isValid() &&
                static_cast<git::RefKind>(kind.toInt()) != git::RefKind::Stash &&
                !item->data(0, kOidRole).toString().isEmpty()) {
                refs << item;
            }
        }
    }
    return refs;
}

QString RefsSidebar::keyOf(const QTreeWidgetItem* item) {
    if (item == nullptr) {
        return {};
    }
    // What the row *is*, not where it sits: a rebuild moves rows whenever a
    // branch appears or a filter changes.
    if (const QVariant kind = item->data(0, kKindRole); kind.isValid()) {
        return QStringLiteral("%1:%2:%3")
            .arg(item->data(0, kPinnedRowRole).toBool() ? QStringLiteral("pin")
                                                        : QStringLiteral("ref"))
            .arg(kind.toInt())
            .arg(item->data(0, kNameRole).toString());
    }
    if (const QVariant screen = item->data(0, kScreenRole); screen.isValid()) {
        return QStringLiteral("row:%1").arg(item->text(0));
    }
    const QTreeWidgetItem* parent = item->parent();
    return QStringLiteral("group:%1/%2").arg(parent != nullptr ? parent->text(0) : QString(),
                                             item->text(0));
}

void RefsSidebar::setRefs(session::RefSetPtr refs) {
    refs_ = std::move(refs);

    // Every refresh rebuilds the tree — a fetch, a commit, a stage changing
    // the Working copy badge — so what the user did to it is carried across:
    // the row they had selected, and the sections they folded away.
    const QString selectedKey = keyOf(currentItem());
    QHash<QString, bool> expansion;
    for (QTreeWidgetItemIterator it(this); *it != nullptr; ++it) {
        if ((*it)->childCount() > 0) {
            expansion.insert(keyOf(*it), (*it)->isExpanded());
        }
    }
    const auto restore = qScopeGuard([this, &selectedKey, &expansion] {
        for (QTreeWidgetItemIterator it(this); *it != nullptr; ++it) {
            const QString key = keyOf(*it);
            if (const auto found = expansion.constFind(key); found != expansion.constEnd()) {
                (*it)->setExpanded(found.value());
            }
            if (!selectedKey.isEmpty() && key == selectedKey) {
                setCurrentItem(*it);
            }
        }
    });

    clear();
    // While filtering, the Workspace rows are noise: they are not refs and
    // cannot match what was typed, so a search for "release" should not leave
    // "Working copy" sitting at the top of the results.
    if (filter_.isEmpty()) {
        addWorkspaceGroup();
    }
    if (!refs_) {
        return;
    }

    // Sections are omitted rather than shown empty: a "Stashes" heading with
    // nothing under it is noise in a panel that is mostly scanned, not read.
    addPinned();
    if (!refs_->localBranches.empty()) {
        addEntries(addSection(tr("Branches")), refs_->localBranches, QStringLiteral("\u2442"));
    }
    if (!refs_->remoteBranches.empty()) {
        addRemoteGroups(addSection(tr("Remotes")), refs_->remoteBranches);
    }
    if (!refs_->tags.empty()) {
        auto* section = addSection(tr("Tags"));
        addEntries(section, refs_->tags, QStringLiteral("\u2317"));
        section->setExpanded(refs_->tags.size() <= 12);
    }
    if (!refs_->submodules.empty()) {
        addSubmodules(addSection(tr("Submodules")), refs_->submodules);
    }
    if (!refs_->stashes.empty()) {
        addEntries(addSection(tr("Stashes")), refs_->stashes, QStringLiteral("\u2261"));
    }

    // A heading with nothing under it says a section exists and is empty,
    // which during a search is a different and wrong claim.
    for (int i = topLevelItemCount() - 1; i >= 0; --i) {
        if (topLevelItem(i)->childCount() == 0) {
            delete takeTopLevelItem(i);
        }
    }

    if (!filter_.isEmpty() && topLevelItemCount() == 0) {
        auto* empty = new QTreeWidgetItem(this);
        empty->setText(0, tr("No ref matches \"%1\"").arg(filter_));
        empty->setFlags(Qt::ItemIsEnabled);
        empty->setData(0, ToneRole, static_cast<int>(SidebarTone::Muted));
    }
}

void RefsSidebar::clearRefs() {
    refs_.reset();
    changedFiles_ = 0;
    clear();
    addWorkspaceGroup();
}

} // namespace gity::ui
