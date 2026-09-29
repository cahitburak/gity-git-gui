// SPEC.md §3 — the two sides of a binary asset comparison.
//
// Decoding lives here rather than in core because QImage is Qt, and it runs on
// the session thread because QT_MAPPING is explicit: never decode in
// paintEvent. A 2048² PSD takes long enough to drop frames.
//
// What this reports is deliberately what we can actually know. The spec's
// example metadata ("RGBA32 → BC7") is Unity *importer* data, which needs the
// Editor; claiming it from a file header would be inventing it.
#pragma once

#include <QImage>
#include <QMetaType>
#include <QString>

#include <memory>

namespace gity::session {

struct ImageSide {
    QImage image;
    QString label;    ///< "HEAD · 7b902ea" or "Working copy".
    QString metadata; ///< Dimensions · format · size, from the bytes we have.

    bool present = false;   ///< The path exists on this side at all.
    bool decoded = false;   ///< ...and Qt could read it as an image.
    bool lfsPointer = false;
    QString lfsOid;
    qint64 byteSize = 0;
};

struct ImageDiff {
    QString path;
    ImageSide before;
    ImageSide after;

    /// True when both sides decoded, which is what the difference and
    /// onion-skin modes require.
    [[nodiscard]] bool comparable() const {
        return before.decoded && after.decoded &&
               before.image.size() == after.image.size();
    }

    /// Percentage of pixels that differ, for the difference mode caption.
    /// Computed once when the pair is built, never on repaint.
    double changedPixelPercent = 0.0;
};

using ImageDiffPtr = std::shared_ptr<const ImageDiff>;

} // namespace gity::session

Q_DECLARE_METATYPE(gity::session::ImageDiffPtr)
