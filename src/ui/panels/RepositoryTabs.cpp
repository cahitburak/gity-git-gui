#include "RepositoryTabs.h"

#include <QDir>
#include <QHBoxLayout>
#include <QTabBar>
#include <QVariantMap>

namespace gity::ui {
namespace {

const QString kPathKey = QStringLiteral("path");
const QString kParentKey = QStringLiteral("parent");

} // namespace

RepositoryTabs::RepositoryTabs(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("repositoryTabs"));
    // The strip is one band across the window; without a styled background
    // the bar's own colour stopped where its tabs did, leaving a box.
    setAttribute(Qt::WA_StyledBackground, true);
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    bar_ = new QTabBar(this);
    bar_->setExpanding(false);
    bar_->setDrawBase(false);
    bar_->setTabsClosable(true);
    // Movable: no tab is special by position. Each carries its own path and,
    // for a submodule, the name of the repository it belongs to.
    bar_->setMovable(true);
    bar_->setUsesScrollButtons(true);
    bar_->setElideMode(Qt::ElideMiddle);
    layout->addWidget(bar_);
    layout->addStretch(1);

    connect(bar_, &QTabBar::currentChanged, this, [this](int index) {
        emit tabsChanged();
        if (suppress_ || index < 0) {
            return;
        }
        emit repositoryActivated(pathAt(index));
    });
    connect(bar_, &QTabBar::tabMoved, this, &RepositoryTabs::tabsChanged);
    connect(bar_, &QTabBar::tabCloseRequested, this, [this](int index) {
        if (bar_->count() <= 1 || index < 0 || index >= bar_->count()) {
            return;
        }
        const bool wasCurrent = index == bar_->currentIndex();
        // Removing the current tab moves the current index, and that change
        // is announced below once — not twice, with a path in between.
        suppress_ = true;
        bar_->removeTab(index);
        suppress_ = false;
        updateClosable();
        emit tabsChanged();
        if (wasCurrent) {
            emit repositoryActivated(pathAt(bar_->currentIndex()));
        }
    });

    setVisible(false);
}

QString RepositoryTabs::canonical(const QString& path) {
    return path.isEmpty() ? path : QDir(path).absolutePath();
}

QString RepositoryTabs::pathAt(int index) const {
    return index < 0 ? QString() : bar_->tabData(index).toMap().value(kPathKey).toString();
}

int RepositoryTabs::indexOf(const QString& path) const {
    const QString wanted = canonical(path);
    for (int i = 0; i < bar_->count(); ++i) {
        if (canonical(pathAt(i)) == wanted) {
            return i;
        }
    }
    return -1;
}

void RepositoryTabs::open(const QString& path, const QString& parentName) {
    if (activate(path)) {
        return;
    }

    const QString name = QDir(path).dirName();
    QVariantMap record;
    record.insert(kPathKey, canonical(path));
    record.insert(kParentKey, parentName);

    suppress_ = true;
    const int index = bar_->addTab(name);
    bar_->setTabData(index, record);
    bar_->setTabToolTip(index, parentName.isEmpty()
                                   ? QDir::toNativeSeparators(path)
                                   : tr("%1 — submodule of %2")
                                         .arg(QDir::toNativeSeparators(path), parentName));
    bar_->setCurrentIndex(index);
    suppress_ = false;

    updateClosable();
    setVisible(true);
    emit tabsChanged();
}

bool RepositoryTabs::activate(const QString& path) {
    const int index = indexOf(path);
    if (index < 0) {
        return false;
    }
    if (index != bar_->currentIndex()) {
        suppress_ = true;
        bar_->setCurrentIndex(index);
        suppress_ = false;
    }
    return true;
}

void RepositoryTabs::updateClosable() {
    const bool closable = bar_->count() > 1;
    for (int i = 0; i < bar_->count(); ++i) {
        if (QWidget* button = bar_->tabButton(i, QTabBar::RightSide); button != nullptr) {
            button->setVisible(closable);
        }
    }
}

QString RepositoryTabs::currentPath() const {
    return pathAt(bar_->currentIndex());
}

QString RepositoryTabs::currentParentName() const {
    const int index = bar_->currentIndex();
    return index < 0 ? QString() : bar_->tabData(index).toMap().value(kParentKey).toString();
}

int RepositoryTabs::count() const {
    return bar_->count();
}

QVariantList RepositoryTabs::state() const {
    QVariantList tabs;
    for (int i = 0; i < bar_->count(); ++i) {
        tabs << bar_->tabData(i);
    }
    return tabs;
}

int RepositoryTabs::currentIndex() const {
    return bar_->currentIndex();
}

} // namespace gity::ui
