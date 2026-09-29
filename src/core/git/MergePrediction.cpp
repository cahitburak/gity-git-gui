#include "MergePrediction.h"

#include <algorithm>

namespace gity::git {

MergePrediction parseMergeTree(int exitCode, std::string_view output) {
    MergePrediction prediction;
    if (exitCode != 0 && exitCode != 1) {
        return prediction;
    }
    prediction.available = true;
    prediction.clean = exitCode == 0;

    std::size_t start = output.find('\0');
    if (start == std::string_view::npos) {
        return prediction; // the tree alone: nothing conflicted
    }
    ++start;
    while (start < output.size()) {
        std::size_t end = output.find('\0', start);
        if (end == std::string_view::npos) {
            end = output.size();
        }
        std::string path(output.substr(start, end - start));
        // A file can be listed more than once (one entry per conflicted
        // stage); it is one file to the person reading the list.
        if (!path.empty() &&
            std::find(prediction.conflicts.begin(), prediction.conflicts.end(), path) ==
                prediction.conflicts.end()) {
            prediction.conflicts.push_back(std::move(path));
        }
        start = end + 1;
    }
    return prediction;
}

bool mergeTreeSupported(int major, int minor) noexcept {
    return major > 2 || (major == 2 && minor >= 38);
}

} // namespace gity::git
