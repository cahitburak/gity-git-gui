// The first-run path: a repository has to arrive before anything else in this
// client has something to show.
//
// The dialog does no work of its own. It collects a URL and a destination,
// derives the folder name the way git does, and refuses to start until the
// destination is somewhere a clone could actually land — the failure modes
// worth catching (a folder that already exists, a parent that does not) are
// all knowable before a byte moves.
#pragma once

#include <QDialog>

class QDialogButtonBox;
class QLabel;
class QLineEdit;

namespace gity::ui {

class CloneDialog : public QDialog {
    Q_OBJECT

public:
    explicit CloneDialog(QWidget* parent = nullptr);

    [[nodiscard]] QString url() const;
    [[nodiscard]] QString parentDirectory() const;
    [[nodiscard]] QString folderName() const;

    /// The folder git would create for `url`, by git's own rule: the last path
    /// segment with a trailing ".git" removed. Exposed for testing the cases
    /// that bite — trailing slashes, scp-style SSH remotes, bare paths.
    [[nodiscard]] static QString folderNameFor(const QString& url);

private:
    void revalidate();

    QLineEdit* url_ = nullptr;
    QLineEdit* directory_ = nullptr;
    QLineEdit* folder_ = nullptr;
    QLabel* status_ = nullptr;
    QDialogButtonBox* buttons_ = nullptr;
    /// True once the user edits the folder themselves, after which the URL no
    /// longer overwrites what they typed.
    bool folderEdited_ = false;
};

} // namespace gity::ui
