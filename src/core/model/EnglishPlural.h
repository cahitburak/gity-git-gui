// English plurals without a translation file.
//
// Strings are written "%n file(s)" for Qt's numerus translation, which picks
// the right form only when a .qm file supplies it. Gity ships none for its
// source language, so without this every count read "1 file(s)", "3 file(s)".
#pragma once

#include <string>
#include <string_view>

namespace gity::model {

/// `text` with each "(s)" resolved for `n`: dropped when n is 1, "s" otherwise.
[[nodiscard]] std::string englishPlural(std::string_view text, int n);

} // namespace gity::model
