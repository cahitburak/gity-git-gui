#include "ImageDiffView.h"

#include "ui/theme/Tokens.h"

#include <QButtonGroup>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QToolButton>
#include <QVBoxLayout>

namespace gity::ui {
namespace {

constexpr int kCheckerSquare = 18;
constexpr int kViewportPadding = 26;
constexpr int kMaxImageEdge = 330;
constexpr int kCaptionHeight = 28;

/// The checkerboard is a cached brush, not a per-paint loop — QT_MAPPING is
/// explicit about that, and it is redrawn behind every image on every frame.
///
/// Keyed by the two colours it is drawn from: a tile built once and kept
/// the previous theme's squares after a switch (2026-09 UI review).
QPixmap checkerBrush() {
    static QPixmap tile;
    static QRgb builtFrom[2] = {0, 0};
    const QRgb panel = tokens::surfacePanel.rgba();
    const QRgb square = tokens::surfaceCheckerboard.rgba();
    if (tile.isNull() || builtFrom[0] != panel || builtFrom[1] != square) {
        builtFrom[0] = panel;
        builtFrom[1] = square;
        tile = QPixmap(kCheckerSquare * 2, kCheckerSquare * 2);
        tile.fill(tokens::surfacePanel);
        QPainter painter(&tile);
        painter.fillRect(0, 0, kCheckerSquare, kCheckerSquare, tokens::surfaceCheckerboard);
        painter.fillRect(kCheckerSquare, kCheckerSquare, kCheckerSquare, kCheckerSquare,
                         tokens::surfaceCheckerboard);
    }
    return tile;
}

QFont monoFont(const QFont& base, int delta = -1) {
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPointSize(std::max(7, base.pointSize() + delta));
    return font;
}

QRect fitSquare(const QRect& area, const QSize& imageSize) {
    if (imageSize.isEmpty()) {
        return {};
    }
    const int edge = std::min({kMaxImageEdge, area.width() - kViewportPadding * 2,
                               area.height() - kViewportPadding * 2});
    if (edge <= 0) {
        return {};
    }
    QSize scaled = imageSize.scaled(edge, edge, Qt::KeepAspectRatio);
    return QRect(area.center() - QPoint(scaled.width() / 2, scaled.height() / 2), scaled);
}

} // namespace

ImageDiffView::ImageDiffView(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* header = new QWidget(this);
    header->setFixedHeight(tokens::chromePaneHeader);
    header->setObjectName(QStringLiteral("paneHeader"));
    auto* headerRow = new QHBoxLayout(header);
    headerRow->setContentsMargins(12, 0, 12, 0);
    headerRow->setSpacing(10);

    path_ = new QLabel(header);
    path_->setFont(monoFont(font()));
    path_->setObjectName(QStringLiteral("panePath"));
    headerRow->addWidget(path_, 1);

    lfsChip_ = new QLabel(header);
    lfsChip_->setVisible(false);
    lfsChip_->setFont(monoFont(font(), -2));
    lfsChip_->setObjectName(QStringLiteral("lfsChip"));
    headerRow->addWidget(lfsChip_);

    // A segmented control: flat checkable buttons in an exclusive group, which
    // is what QT_MAPPING suggests over a bespoke widget.
    modeBar_ = new QWidget(header);
    auto* modeRow = new QHBoxLayout(modeBar_);
    modeRow->setContentsMargins(0, 0, 0, 0);
    modeRow->setSpacing(0);
    auto* group = new QButtonGroup(this);
    group->setExclusive(true);

    const std::pair<const char*, Mode> modes[] = {
        {"Side by side", Mode::SideBySide},
        {"Difference", Mode::Difference},
        {"Onion skin", Mode::OnionSkin},
    };
    for (const auto& [label, mode] : modes) {
        auto* button = new QToolButton(modeBar_);
        button->setText(tr(label));
        button->setCheckable(true);
        button->setChecked(mode == Mode::SideBySide);
        button->setAutoRaise(true);
        group->addButton(button);
        modeRow->addWidget(button);
        connect(button, &QToolButton::clicked, this, [this, m = mode] { setMode(m); });
    }
    headerRow->addWidget(modeBar_);

    layout->addWidget(header);
    layout->addStretch();
}

void ImageDiffView::setMode(Mode mode) {
    if (mode_ == mode) {
        return;
    }
    mode_ = mode;
    rebuildComposite();
    update();
}

void ImageDiffView::setDiff(session::ImageDiffPtr diff) {
    diff_ = std::move(diff);
    path_->setText(diff_ ? diff_->path : QString());

    const bool lfs = diff_ && (diff_->before.lfsPointer || diff_->after.lfsPointer);
    lfsChip_->setVisible(lfs);
    // Lock holder needs the LFS lock API, which arrives with M4. Saying "LFS"
    // without inventing a name is the honest half.
    lfsChip_->setText(tr("LFS"));

    rebuildComposite();
    update();
}

void ImageDiffView::clearDiff() {
    diff_.reset();
    composite_ = QImage();
    path_->clear();
    lfsChip_->setVisible(false);
    update();
}

void ImageDiffView::rebuildComposite() {
    composite_ = QImage();
    if (!diff_ || !diff_->comparable() || mode_ == Mode::SideBySide) {
        return;
    }

    composite_ = diff_->before.image.convertToFormat(QImage::Format_ARGB32);
    QPainter painter(&composite_);
    if (mode_ == Mode::Difference) {
        painter.setCompositionMode(QPainter::CompositionMode_Difference);
        painter.drawImage(0, 0, diff_->after.image);
    } else {
        painter.setOpacity(0.5);
        painter.drawImage(0, 0, diff_->after.image);
    }
}

void ImageDiffView::paintPane(QPainter& painter, const QRect& area, const QString& label,
                                const QString& metadata, const QColor& labelColour,
                                const QImage& image) const {
    // Caption bar first: the label says which side you are looking at, and a
    // pane without one is unreadable the moment there are two.
    const QRect caption(area.x(), area.y(), area.width(), kCaptionHeight);
    painter.fillRect(caption, tokens::surfaceWindow);

    QFont labelFont = font();
    labelFont.setCapitalization(QFont::AllUppercase);
    labelFont.setWeight(QFont::DemiBold);
    labelFont.setPointSize(std::max(7, font().pointSize() - 1));
    painter.setFont(labelFont);
    painter.setPen(labelColour);
    painter.drawText(caption.adjusted(12, 0, -12, 0), Qt::AlignVCenter | Qt::AlignLeft, label);

    painter.setFont(monoFont(font(), -2));
    painter.setPen(tokens::textLabel);
    painter.drawText(caption.adjusted(12, 0, -12, 0), Qt::AlignVCenter | Qt::AlignRight,
                     metadata);

    const QRect viewport = area.adjusted(0, kCaptionHeight, 0, 0);
    painter.fillRect(viewport, QBrush(checkerBrush()));

    if (image.isNull()) {
        painter.setPen(tokens::textFaint);
        painter.drawText(viewport, Qt::AlignCenter,
                         tr("no preview"));
        return;
    }

    const QRect target = fitSquare(viewport, image.size());
    if (target.isEmpty()) {
        return;
    }
    painter.drawImage(target, image);
    painter.setPen(QPen(tokens::borderControl, 1, Qt::DashLine));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(target.adjusted(-1, -1, 1, 1));
}

void ImageDiffView::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    const QRect body(0, tokens::chromePaneHeader, width(), height() - tokens::chromePaneHeader);
    painter.fillRect(body, tokens::surfaceSunken);

    if (!diff_) {
        painter.setPen(tokens::textFaint);
        painter.drawText(body, Qt::AlignCenter, tr("Select a binary asset to compare"));
        return;
    }

    if (mode_ == Mode::SideBySide) {
        const int half = body.width() / 2;
        paintPane(painter, QRect(body.x(), body.y(), half, body.height()), diff_->before.label,
                  diff_->before.metadata, tokens::textDim, diff_->before.image);
        paintPane(painter, QRect(body.x() + half, body.y(), body.width() - half, body.height()),
                  diff_->after.label, diff_->after.metadata, tokens::semanticAdd,
                  diff_->after.image);

        painter.setPen(tokens::borderHard);
        painter.drawLine(body.x() + half, body.y(), body.x() + half, body.bottom());
        return;
    }

    if (!diff_->comparable()) {
        // Different dimensions, or one side undecodable. Saying so beats
        // showing a blend of things that do not correspond.
        painter.setPen(tokens::textFaint);
        painter.drawText(body, Qt::AlignCenter,
                         tr("The two versions cannot be blended — %1")
                             .arg(diff_->before.decoded && diff_->after.decoded
                                      ? tr("their dimensions differ")
                                      : tr("one side is not a decodable image")));
        return;
    }

    const QString label = mode_ == Mode::Difference ? tr("Difference blend")
                                                    : tr("Onion skin · 50%");
    const QString metadata =
        mode_ == Mode::Difference
            ? tr("%1 × %2 · %3% of pixels changed")
                  .arg(composite_.width())
                  .arg(composite_.height())
                  .arg(diff_->changedPixelPercent, 0, 'f', 1)
            : tr("%1 × %2 · HEAD under working copy")
                  .arg(composite_.width())
                  .arg(composite_.height());

    paintPane(painter, body, label, metadata, tokens::semanticWarn, composite_);
}

} // namespace gity::ui
