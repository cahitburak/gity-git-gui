#include "LaneAssigner.h"

#include <algorithm>

namespace gity::graph {

std::uint16_t LaneAssigner::takeFreeSlot() {
    for (std::size_t s = 0; s < slots_.size(); ++s) {
        if (!slots_[s].occupied) {
            return static_cast<std::uint16_t>(s);
        }
    }
    slots_.emplace_back();
    return static_cast<std::uint16_t>(slots_.size() - 1);
}

void LaneAssigner::append(CommitKey key, std::span<const CommitKey> parents, Graph& out) {
    const auto edgeBegin = static_cast<std::uint32_t>(out.edges.size());
    std::uint16_t flags = RowFlag_None;

    const auto awaits = [&](std::size_t s) {
        return slots_[s].occupied && slots_[s].awaiting && slots_[s].key == key;
    };

    // 1. Leftmost slot awaiting this commit claims it.
    std::uint16_t myLane = kNoLane;
    for (std::size_t s = 0; s < slots_.size(); ++s) {
        if (awaits(s)) {
            myLane = static_cast<std::uint16_t>(s);
            break;
        }
    }
    if (myLane == kNoLane) {
        // Nothing was waiting for this commit: it is a branch tip. It occupies
        // a column, but no line arrives into it from above.
        myLane = takeFreeSlot();
        slots_[myLane] = Slot{key, true, false};
        flags |= RowFlag_Tip;
    }

    // 2. Every slot awaiting this commit produces an incoming edge; the ones
    //    that are not myLane close here. This is a merge seen from below.
    for (std::size_t s = 0; s < slots_.size(); ++s) {
        if (awaits(s)) {
            out.edges.push_back({static_cast<std::uint16_t>(s), myLane, EdgeKind::In});
            if (s != myLane) {
                slots_[s].occupied = false;
                slots_[s].awaiting = false;
            }
        }
    }

    // 3. Lanes untouched by this commit cross the row.
    for (std::size_t s = 0; s < slots_.size(); ++s) {
        if (s == myLane || !slots_[s].occupied) {
            continue;
        }
        const auto lane = static_cast<std::uint16_t>(s);
        out.edges.push_back({lane, lane, EdgeKind::PassThrough});
    }

    // 4. First parent inherits the lane; the rest open new ones.
    if (parents.size() > 1) {
        flags |= RowFlag_Merge;
    }
    if (parents.empty()) {
        flags |= RowFlag_Root;
        slots_[myLane] = Slot{};
    }

    for (std::size_t p = 0; p < parents.size(); ++p) {
        const CommitKey parent = parents[p];
        std::uint16_t lane = kNoLane;

        if (p == 0) {
            lane = myLane;
            slots_[myLane] = Slot{parent, true, true};
        } else {
            // Reuse a slot already awaiting this parent so a commit reachable
            // by two routes does not open a duplicate lane.
            for (std::size_t s = 0; s < slots_.size(); ++s) {
                if (slots_[s].occupied && slots_[s].awaiting && slots_[s].key == parent) {
                    lane = static_cast<std::uint16_t>(s);
                    break;
                }
            }
            if (lane == kNoLane) {
                lane = takeFreeSlot();
                slots_[lane] = Slot{parent, true, true};
            }
        }
        out.edges.push_back({myLane, lane, EdgeKind::Out});
    }

    // 5. Band width is whatever this row's own dot and edges touch.
    auto laneCount = static_cast<std::uint16_t>(myLane + 1);
    for (std::size_t e = edgeBegin; e < out.edges.size(); ++e) {
        laneCount = std::max({laneCount,
                              static_cast<std::uint16_t>(out.edges[e].from + 1),
                              static_cast<std::uint16_t>(out.edges[e].to + 1)});
    }

    GraphRow row{};
    row.commitIndex = static_cast<CommitIndex>(out.rows.size());
    row.edgeOffset = edgeBegin;
    row.lane = myLane;
    row.laneCount = laneCount;
    row.edgeCount = static_cast<std::uint16_t>(out.edges.size() - edgeBegin);
    row.flags = flags;
    out.rows.push_back(row);

    while (!slots_.empty() && !slots_.back().occupied) {
        slots_.pop_back();
    }
}

Graph LaneAssigner::assign(const CommitTable& commits) {
    Graph graph;
    const std::size_t n = commits.size();
    graph.rows.reserve(n);
    graph.edges.reserve(n * 2);

    LaneAssigner assigner;
    for (std::size_t i = 0; i < n; ++i) {
        const std::uint32_t begin = commits.parentOffset[i];
        const std::uint32_t end = commits.parentOffset[i + 1];
        assigner.append(commits.key[i],
                        std::span<const CommitKey>(commits.parentKey.data() + begin, end - begin),
                        graph);
    }
    return graph;
}

} // namespace gity::graph
