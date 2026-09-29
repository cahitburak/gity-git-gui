#include "EnglishPlural.h"

namespace gity::model {

std::string englishPlural(std::string_view text, int n) {
    constexpr std::string_view marker = "(s)";
    std::string out;
    out.reserve(text.size());
    std::size_t at = 0;
    while (true) {
        const std::size_t found = text.find(marker, at);
        out.append(text.substr(at, found == std::string_view::npos ? text.npos : found - at));
        if (found == std::string_view::npos) {
            break;
        }
        if (n != 1) {
            out += 's';
        }
        at = found + marker.size();
    }
    return out;
}

} // namespace gity::model
