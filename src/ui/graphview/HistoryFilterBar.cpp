#include "HistoryFilterBar.h"

#include "ui/panels/Confirm.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateEdit>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace gity::ui {

HistoryFilterBar::HistoryFilterBar(QWidget* parent) : QWidget(parent) {
    // Named for the stylesheet, and for the window: clicking the header when
    // the graph has been dragged down to it brings the graph back.
    setObjectName(QStringLiteral("historyHeader"));
    setAttribute(Qt::WA_StyledBackground, true);
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(12, 5, 8, 5);

    title_ = new QLabel(tr("All commits"), this);
    title_->setObjectName(QStringLiteral("historyTitle"));
    note_ = new QLabel(this);
    applyRole(note_, QStringLiteral("note"));
    pathChip_ = new QToolButton(this);
    pathChip_->setObjectName(QStringLiteral("historyPath"));
    pathChip_->setAutoRaise(true);
    pathChip_->setVisible(false);
    // The note beside it gives way first; the path is what was asked for.
    pathChip_->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    connect(pathChip_, &QToolButton::clicked, this, [this] { showPath({}); });

    debounce_ = new QTimer(this);
    debounce_->setSingleShot(true);
    debounce_->setInterval(kDebounceMs);
    connect(debounce_, &QTimer::timeout, this, &HistoryFilterBar::filterChanged);

    // Who: every contributor the history has, most commits first, and free
    // text for part of a name.
    author_ = new QComboBox(this);
    author_->setEditable(true);
    author_->setInsertPolicy(QComboBox::NoInsert);
    author_->setFixedWidth(170);
    author_->lineEdit()->setPlaceholderText(tr("Any author"));
    author_->lineEdit()->setClearButtonEnabled(true);
    author_->setToolTip(tr("Show only commits by this contributor. Part of a name works "
                           "too."));
    connect(author_, &QComboBox::currentTextChanged, this, [this] { debounce_->start(); });

    // When: presets for the usual questions, and a range for the rest.
    date_ = new QToolButton(this);
    date_->setPopupMode(QToolButton::InstantPopup);
    date_->setToolTip(tr("Show only commits made in this period."));
    auto* dates = new QMenu(date_);
    const auto preset = [this, dates](const QString& label, int days) {
        dates->addAction(label, this, [this, days] {
            since_ = days == 0 ? 0
                               : QDateTime(QDate::currentDate().addDays(-days), QTime(0, 0))
                                     .toSecsSinceEpoch();
            until_ = 0;
            refreshDateLabel();
            changedNow();
        });
    };
    preset(tr("Any Time"), 0);
    dates->addSeparator();
    preset(tr("Last 7 Days"), 7);
    preset(tr("Last 30 Days"), 30);
    preset(tr("Last 12 Months"), 365);
    dates->addSeparator();
    dates->addAction(tr("Custom Range…"), this, &HistoryFilterBar::chooseDateRange);
    date_->setMenu(dates);
    refreshDateLabel();

    text_ = new QLineEdit(this);
    text_->setPlaceholderText(tr("Filter commits…"));
    text_->setFixedWidth(210);
    text_->setClearButtonEnabled(true);
    text_->setToolTip(
        tr("Show only commits whose message, author or id matches.\n"
           "A filtered view is a list of matches, not a graph — the commits between two "
           "matches are missing, so no lines are drawn between them."));
    connect(text_, &QLineEdit::textChanged, this, [this] { debounce_->start(); });

    layout->addWidget(title_);
    layout->addWidget(pathChip_);
    layout->addWidget(note_, 1);
    layout->addWidget(author_);
    layout->addWidget(date_);
    layout->addWidget(text_);
}

gity::git::CommitFilter HistoryFilterBar::filter() const {
    gity::git::CommitFilter filter;
    filter.text = text_->text().trimmed().toStdString();
    filter.author = author_->currentText().trimmed().toStdString();
    filter.since = since_;
    filter.until = until_;
    filter.path = path_.toStdString();
    return filter;
}

void HistoryFilterBar::setFilter(const gity::git::CommitFilter& filter) {
    const QSignalBlocker quietText(text_);
    const QSignalBlocker quietAuthor(author_);
    debounce_->stop();
    text_->setText(QString::fromStdString(filter.text));
    author_->setCurrentText(QString::fromStdString(filter.author));
    since_ = filter.since;
    until_ = filter.until;
    path_ = QString::fromStdString(filter.path);
    refreshDateLabel();
    showState(filter.active());
}

void HistoryFilterBar::showState(bool filtered) {
    // Said on screen, not only in a tooltip: a filtered list has no graph
    // lines, and that is by design rather than a rendering fault.
    const bool byPath = !path_.isEmpty();
    pathChip_->setVisible(byPath);
    if (byPath) {
        // The header is crowded at narrow widths. A deep path gives up its
        // middle folders first, and its file name last.
        const QFontMetrics fm = pathChip_->fontMetrics();
        constexpr int kRoom = 170;
        QString shown = path_;
        const QString name = path_.section(QChar('/'), -1);
        if (fm.horizontalAdvance(shown) > kRoom && path_.count(QChar('/')) > 1) {
            shown = path_.section(QChar('/'), 0, 0) + QStringLiteral("/…/") + name;
        }
        if (fm.horizontalAdvance(shown) > kRoom && name != path_) {
            shown = QStringLiteral("…/") + name;
        }
        shown = fm.elidedText(shown, Qt::ElideRight, kRoom);
        const QString text = QStringLiteral("%1  ✕").arg(shown);
        pathChip_->setText(text);
        // Wide enough for the text as shortened here: left to the layout, the
        // style cut it again, mid-name.
        pathChip_->setMinimumWidth(fm.horizontalAdvance(text) + 24);
        pathChip_->setToolTip(plainToolTip(tr("%1\n\nOnly the commits that changed it — a "
                                              "file is followed back through renames — and no "
                                              "graph lines are drawn. Click to show every file "
                                              "again.")
                                               .arg(path_)));
    }
    title_->setText(byPath     ? tr("History of")
                    : filtered ? tr("Matching commits")
                               : tr("All commits"));
    // With a path the chip's tooltip says what the note would, and the
    // header has no room for both beside the filters.
    note_->setText(filtered && !byPath ? tr("The commits between matches are not shown, so no "
                                            "graph lines join them.")
                                       : QString());
}

void HistoryFilterBar::showPath(const QString& path) {
    path_ = path;
    showState(filter().active());
    changedNow();
}

void HistoryFilterBar::setContributors(const QList<QPair<QString, int>>& contributors) {
    const QSignalBlocker quiet(author_);
    const QString typed = author_->currentText();
    author_->clear();
    for (const auto& [name, count] : contributors) {
        author_->addItem(name);
        author_->setItemData(author_->count() - 1, tr("%n commit(s)", nullptr, count),
                             Qt::ToolTipRole);
    }
    author_->setCurrentText(typed);
}

void HistoryFilterBar::changedNow() {
    debounce_->stop();
    emit filterChanged();
}

void HistoryFilterBar::refreshDateLabel() {
    const auto day = [](qint64 secs) {
        return QLocale().toString(QDateTime::fromSecsSinceEpoch(secs).date(), QLocale::ShortFormat);
    };
    QString label;
    if (since_ == 0 && until_ == 0) {
        label = tr("Any time");
    } else if (until_ == 0) {
        label = tr("Since %1").arg(day(since_));
    } else if (since_ == 0) {
        label = tr("Until %1").arg(day(until_));
    } else {
        label = tr("%1 – %2").arg(day(since_), day(until_));
    }
    date_->setText(label);
}

void HistoryFilterBar::chooseDateRange() {
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Commits Made Between"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout;
    const QDate today = QDate::currentDate();
    const auto dateEdit = [&dialog](const QDate& date) {
        auto* edit = new QDateEdit(date, &dialog);
        edit->setCalendarPopup(true);
        edit->setDisplayFormat(QLocale().dateFormat(QLocale::ShortFormat));
        return edit;
    };
    auto* fromOn = new QCheckBox(tr("From"), &dialog);
    auto* from =
        dateEdit(since_ != 0 ? QDateTime::fromSecsSinceEpoch(since_).date() : today.addMonths(-1));
    auto* toOn = new QCheckBox(tr("To"), &dialog);
    auto* to = dateEdit(until_ != 0 ? QDateTime::fromSecsSinceEpoch(until_).date() : today);
    fromOn->setChecked(since_ != 0 || until_ == 0);
    toOn->setChecked(until_ != 0);
    form->addRow(fromOn, from);
    form->addRow(toOn, to);
    layout->addLayout(form);
    auto* note = new QLabel(tr("Both days included. Leave one unticked for an open end."), &dialog);
    note->setWordWrap(true);
    applyRole(note, QStringLiteral("note"));
    layout->addWidget(note);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dialog);
    QPushButton* go = buttons->addButton(tr("Show"), QDialogButtonBox::AcceptRole);
    go->setDefault(true);
    applyRole(go, QStringLiteral("primary"));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    const auto sync = [=] {
        from->setEnabled(fromOn->isChecked());
        to->setEnabled(toOn->isChecked());
        // A range that ends before it starts matches nothing, which reads
        // as a broken filter rather than as the typo it is.
        go->setEnabled(!(fromOn->isChecked() && toOn->isChecked() && to->date() < from->date()));
    };
    for (QCheckBox* box : {fromOn, toOn}) {
        connect(box, &QCheckBox::toggled, &dialog, sync);
    }
    for (QDateEdit* edit : {from, to}) {
        connect(edit, &QDateEdit::dateChanged, &dialog, sync);
    }
    sync();
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    since_ = fromOn->isChecked() ? QDateTime(from->date(), QTime(0, 0)).toSecsSinceEpoch() : 0;
    // The whole of the last day.
    until_ = toOn->isChecked() ? QDateTime(to->date(), QTime(23, 59, 59)).toSecsSinceEpoch() : 0;
    refreshDateLabel();
    changedNow();
}

} // namespace gity::ui
