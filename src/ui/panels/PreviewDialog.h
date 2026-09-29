// "Here is what will happen" — shown before a pull, merge, rebase or push.
//
// A plain sentence first, then the evidence: the commits involved, the files
// a trial merge says would conflict, the uncommitted files that would stop
// git, and the command that will run. The caller decides which choices to
// offer; this only lays them out.
#pragma once

#include <QDialog>
#include <QStringList>

class QHBoxLayout;
class QVBoxLayout;
class QPushButton;

namespace gity::ui {

class PreviewDialog : public QDialog {
    Q_OBJECT

public:
    explicit PreviewDialog(const QString& title, QWidget* parent = nullptr);

    void setHeadline(const QString& text);
    /// A paragraph. `role` is a label role from gity.qss — note, warn, error.
    void addText(const QString& text, const QString& role = {});
    /// A heading and a short list, commits or file names, in a fixed font.
    void addList(const QString& heading, const QStringList& items, int total = -1,
                 const QString& role = {});
    /// The git command the main choice runs, as the command log will show it.
    void setCommand(const QString& command);
    /// A button. `role` is primary or destructive; the first primary is the
    /// default, so Enter takes the ordinary path.
    void addChoice(const QString& label, int id, const QString& role = {});

    /// Shows the dialog; the chosen id, or -1 for Cancel.
    int choose();

private:
    QVBoxLayout* body_ = nullptr;
    QHBoxLayout* buttons_ = nullptr;
    QString command_;
    int chosen_ = -1;
    bool hasDefault_ = false;
};

} // namespace gity::ui
