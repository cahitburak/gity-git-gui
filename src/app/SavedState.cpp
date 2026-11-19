#include "SavedState.h"

#include "ui/panels/RepositoryTabs.h"

#include <QPair>
#include <QSettings>

#include <algorithm>

namespace gity::app::saved {
namespace {

const QString kRecentKey = QStringLiteral("recent/repositories");
const QString kPinnedKey = QStringLiteral("pinnedBranches");

QSettings settings() {
    return QSettings(QStringLiteral("Gity"), QStringLiteral("Gity"));
}

/// Every repository's pins, as stored: (working directory, full ref names).
QList<QPair<QString, QStringList>> readPinned(QSettings& store) {
    QList<QPair<QString, QStringList>> all;
    const int count = store.beginReadArray(kPinnedKey);
    for (int i = 0; i < count; ++i) {
        store.setArrayIndex(i);
        all.append({store.value(QStringLiteral("repository")).toString(),
                    store.value(QStringLiteral("branches")).toStringList()});
    }
    store.endArray();
    return all;
}

} // namespace

QStringList recentRepositories() {
    return settings().value(kRecentKey).toStringList();
}

QStringList rememberRepository(const QString& path) {
    QStringList recent = recentRepositories();
    const QString canonical = gity::ui::RepositoryTabs::canonical(path);
    recent.erase(std::remove_if(recent.begin(), recent.end(),
                                [&canonical](const QString& entry) {
                                    return gity::ui::RepositoryTabs::canonical(entry) == canonical;
                                }),
                 recent.end());
    recent.prepend(canonical);
    while (recent.size() > kRecentLimit) {
        recent.removeLast();
    }
    settings().setValue(kRecentKey, recent);
    return recent;
}

void clearRecentRepositories() {
    settings().remove(kRecentKey);
}

QStringList pinnedBranches(const QString& workdir) {
    QSettings store = settings();
    for (const auto& [repository, branches] : readPinned(store)) {
        if (repository == workdir) {
            return branches;
        }
    }
    return {};
}

void setPinnedBranches(const QString& workdir, const QStringList& pinned) {
    QSettings store = settings();
    QList<QPair<QString, QStringList>> all = readPinned(store);
    all.erase(std::remove_if(all.begin(), all.end(),
                             [&workdir](const auto& entry) { return entry.first == workdir; }),
              all.end());
    if (!pinned.isEmpty()) {
        all.append({workdir, pinned});
    }
    store.remove(kPinnedKey);
    store.beginWriteArray(kPinnedKey, static_cast<int>(all.size()));
    for (int i = 0; i < all.size(); ++i) {
        store.setArrayIndex(i);
        store.setValue(QStringLiteral("repository"), all[i].first);
        store.setValue(QStringLiteral("branches"), all[i].second);
    }
    store.endArray();
}

Tabs openTabs() {
    const QSettings store = settings();
    const QStringList paths = store.value(QStringLiteral("session/tabs")).toStringList();
    const QStringList parents = store.value(QStringLiteral("session/parents")).toStringList();
    Tabs saved;
    saved.current = store.value(QStringLiteral("session/current"), 0).toInt();
    for (int i = 0; i < paths.size(); ++i) {
        saved.tabs.append({paths.at(i), i < parents.size() ? parents.at(i) : QString()});
    }
    return saved;
}

void saveOpenTabs(const Tabs& tabs) {
    QStringList paths;
    QStringList parents;
    for (const Tab& tab : tabs.tabs) {
        paths << tab.path;
        parents << tab.parent;
    }
    QSettings store = settings();
    store.setValue(QStringLiteral("session/tabs"), paths);
    store.setValue(QStringLiteral("session/parents"), parents);
    store.setValue(QStringLiteral("session/current"), tabs.current);
}

QByteArray splitterState(const QString& name) {
    return settings().value(QStringLiteral("layout/%1").arg(name)).toByteArray();
}

void saveSplitterState(const QString& name, const QByteArray& state) {
    settings().setValue(QStringLiteral("layout/%1").arg(name), state);
}

} // namespace gity::app::saved
