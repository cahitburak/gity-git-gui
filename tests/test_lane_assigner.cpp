// ADR-005 — the worked example from the architecture record, as an executable
// assertion. If this test and the document ever disagree, one of them is a bug.
//
//   r0  ●──┐      M  — merge of C and E
//       │  │
//   r1  ●  │      C  — main
//       │  │
//   r2  │  ●      E  — feature
//       │  │
//   r3  │  ●      D  — feature
//       │  │
//   r4  ●──┘      B
//       │
//   r5  ●         A  — root

#include "core/graph/LaneAssigner.h"

#include "core/git/HistoryStream.h"

#include <gtest/gtest.h>

#include <initializer_list>
#include <vector>

namespace {

using namespace gity::graph;

// Commits are identified by key, not by row index. Tests use small integers
// as keys; production uses the leading 8 bytes of the OID.
constexpr CommitKey M = 0;
constexpr CommitKey C = 1;
constexpr CommitKey E = 2;
constexpr CommitKey D = 3;
constexpr CommitKey B = 4;
constexpr CommitKey A = 5;

CommitTable makeTable(std::initializer_list<std::vector<CommitKey>> parentsPerCommit) {
    CommitTable table;
    table.parentOffset.push_back(0);
    CommitKey key = 0;
    for (const auto& parents : parentsPerCommit) {
        table.key.push_back(key++);
        for (const CommitKey parent : parents) {
            table.parentKey.push_back(parent);
        }
        table.parentOffset.push_back(static_cast<std::uint32_t>(table.parentKey.size()));
    }
    return table;
}

/// The ADR-005 example history.
CommitTable adrExample() {
    return makeTable({
        {C, E}, // M — merge
        {B},    // C
        {D},    // E
        {B},    // D
        {A},    // B
        {},     // A — root
    });
}

std::size_t countEdges(const Graph& graph, const GraphRow& row, EdgeKind kind) {
    std::size_t n = 0;
    for (std::uint16_t i = 0; i < row.edgeCount; ++i) {
        if (graph.edges[row.edgeOffset + i].kind == kind) {
            ++n;
        }
    }
    return n;
}

TEST(LaneAssigner, AssignsTheLanesFromTheArchitectureRecord) {
    const Graph graph = LaneAssigner::assign(adrExample());

    ASSERT_EQ(graph.rows.size(), 6u);

    // The lane column from the diagram, top to bottom.
    const std::vector<std::uint16_t> expected{0, 0, 1, 1, 0, 0};
    for (std::size_t i = 0; i < expected.size(); ++i) {
        EXPECT_EQ(graph.rows[i].lane, expected[i]) << "row r" << i;
    }
}

TEST(LaneAssigner, SecondParentOpensALane) {
    const Graph graph = LaneAssigner::assign(adrExample());
    const GraphRow& r0 = graph.rows[M];

    EXPECT_TRUE((r0.flags & RowFlag_Merge) != 0);
    EXPECT_TRUE((r0.flags & RowFlag_Tip) != 0) << "M is a branch tip: no lane awaited it";

    // One outgoing edge per parent: lane 0 continues, lane 1 opens.
    EXPECT_EQ(countEdges(graph, r0, EdgeKind::Out), 2u);
    EXPECT_EQ(countEdges(graph, r0, EdgeKind::In), 0u);
    EXPECT_EQ(r0.laneCount, 2u);
}

TEST(LaneAssigner, ConvergingSlotsCloseTheRightmostLane) {
    const Graph graph = LaneAssigner::assign(adrExample());

    // After r3 both slots await B, so r4 receives two incoming edges and the
    // leftmost lane claims the commit.
    const GraphRow& r4 = graph.rows[B];
    EXPECT_EQ(r4.lane, 0u);
    EXPECT_EQ(countEdges(graph, r4, EdgeKind::In), 2u) << "lane 1 converges into lane 0";
    EXPECT_EQ(countEdges(graph, r4, EdgeKind::Out), 1u);

    // ...and lane 1 is gone by the root.
    const GraphRow& r5 = graph.rows[A];
    EXPECT_EQ(r5.laneCount, 1u);
    EXPECT_TRUE((r5.flags & RowFlag_Root) != 0);
    EXPECT_EQ(countEdges(graph, r5, EdgeKind::Out), 0u);
}

TEST(LaneAssigner, RowsArePaintableInIsolation) {
    const Graph graph = LaneAssigner::assign(adrExample());

    // The property the virtualized view depends on: every edge a row declares
    // stays inside that row's own band, so painting can start at any index.
    for (std::size_t i = 0; i < graph.rows.size(); ++i) {
        const GraphRow& row = graph.rows[i];
        EXPECT_LE(static_cast<std::size_t>(row.edgeOffset) + row.edgeCount, graph.edges.size());
        EXPECT_LT(row.lane, row.laneCount) << "row r" << i;
        for (std::uint16_t e = 0; e < row.edgeCount; ++e) {
            const GraphEdge& edge = graph.edges[row.edgeOffset + e];
            EXPECT_LT(edge.from, row.laneCount) << "row r" << i;
            EXPECT_LT(edge.to, row.laneCount) << "row r" << i;
        }
    }
}

TEST(LaneAssigner, LinearHistoryStaysInOneLane) {
    const Graph graph = LaneAssigner::assign(makeTable({{1}, {2}, {3}, {}}));

    ASSERT_EQ(graph.rows.size(), 4u);
    for (const auto& row : graph.rows) {
        EXPECT_EQ(row.lane, 0u);
        EXPECT_EQ(row.laneCount, 1u);
    }
}

TEST(LaneAssigner, HandlesAnEmptyWalk) {
    const Graph graph = LaneAssigner::assign(CommitTable{});
    EXPECT_TRUE(graph.rows.empty());
    EXPECT_TRUE(graph.edges.empty());
}

TEST(LaneAssigner, OctopusMergeOpensOneLanePerExtraParent) {
    // M with four parents, each an independent root.
    const Graph graph = LaneAssigner::assign(makeTable({
        {1, 2, 3, 4},
        {},
        {},
        {},
        {},
    }));

    const GraphRow& merge = graph.rows[0];
    EXPECT_TRUE((merge.flags & RowFlag_Merge) != 0);
    EXPECT_EQ(countEdges(graph, merge, EdgeKind::Out), 4u);
    EXPECT_EQ(merge.laneCount, 4u);
}

TEST(LaneAssigner, RowRecordStaysWithinTheMemoryBudget) {
    // ADR-005 budgets ~24 bytes/row. M0 confirmed the struct at 16; it is the
    // *edge* arena that scales with lane count, which the record now says.
    EXPECT_LE(sizeof(GraphRow), 24u);
}

// --- streaming ------------------------------------------------------------
//
// M0 measured a cold 1M-commit walk at ~4 s, so M1 paints rows as the walk
// produces them. That is only sound if appending in batches gives byte-for-byte
// the same graph as assigning the whole table at once.

TEST(LaneAssigner, IncrementalAppendMatchesWholeTableAssignment) {
    const CommitTable table = adrExample();
    const Graph batch = LaneAssigner::assign(table);

    for (std::size_t chunkSize : {1u, 2u, 3u, 5u, 100u}) {
        Graph streamed;
        LaneAssigner assigner;
        std::size_t emitted = 0;
        while (emitted < table.size()) {
            const std::size_t upto = std::min(emitted + chunkSize, table.size());
            for (std::size_t i = emitted; i < upto; ++i) {
                const std::uint32_t b = table.parentOffset[i];
                const std::uint32_t e = table.parentOffset[i + 1];
                assigner.append(table.key[i],
                                std::span<const CommitKey>(table.parentKey.data() + b, e - b),
                                streamed);
            }
            emitted = upto;
        }

        ASSERT_EQ(streamed.rows.size(), batch.rows.size()) << "chunk " << chunkSize;
        ASSERT_EQ(streamed.edges.size(), batch.edges.size()) << "chunk " << chunkSize;
        for (std::size_t i = 0; i < batch.rows.size(); ++i) {
            EXPECT_EQ(streamed.rows[i].lane, batch.rows[i].lane) << "chunk " << chunkSize
                                                                 << " row " << i;
            EXPECT_EQ(streamed.rows[i].laneCount, batch.rows[i].laneCount);
            EXPECT_EQ(streamed.rows[i].flags, batch.rows[i].flags);
            EXPECT_EQ(streamed.rows[i].edgeCount, batch.rows[i].edgeCount);
        }
        for (std::size_t i = 0; i < batch.edges.size(); ++i) {
            EXPECT_EQ(streamed.edges[i].from, batch.edges[i].from);
            EXPECT_EQ(streamed.edges[i].to, batch.edges[i].to);
            EXPECT_EQ(streamed.edges[i].kind, batch.edges[i].kind);
        }
    }
}

TEST(LaneAssigner, ResetClearsSlotState) {
    LaneAssigner assigner;
    Graph first;
    const CommitTable table = adrExample();
    assigner.append(table.key[0], std::span<const CommitKey>(table.parentKey.data(), 2), first);
    EXPECT_GT(assigner.activeLanes(), 0u);

    assigner.reset();
    EXPECT_EQ(assigner.activeLanes(), 0u);

    // A fresh graph after reset matches a fresh assigner.
    Graph afterReset;
    assigner.append(table.key[0], std::span<const CommitKey>(table.parentKey.data(), 2), afterReset);
    EXPECT_EQ(afterReset.rows[0].lane, first.rows[0].lane);
    EXPECT_EQ(afterReset.edges.size(), first.edges.size());
}

TEST(LaneAssigner, KeysAreIdentitiesNotPositions) {
    // Real keys are truncated OIDs: large, sparse, unrelated to row order.
    CommitTable table;
    table.key = {0xDEADBEEFCAFEULL, 0x1234567890ABULL};
    table.parentOffset = {0, 1, 1};
    table.parentKey = {0x1234567890ABULL};

    const Graph graph = LaneAssigner::assign(table);
    ASSERT_EQ(graph.rows.size(), 2u);
    EXPECT_EQ(graph.rows[0].lane, 0u);
    EXPECT_EQ(graph.rows[1].lane, 0u) << "the child's parent key must claim the same lane";
    EXPECT_TRUE((graph.rows[1].flags & RowFlag_Root) != 0);
}

} // namespace

// --- commit filtering ------------------------------------------------------

TEST(CommitMatches, EmptyFilterKeepsEverything) {
    EXPECT_TRUE(gity::git::commitMatches("any", "anyone", "abc123", ""));
}

TEST(CommitMatches, MatchesSummaryAuthorAndIdPrefix) {
    EXPECT_TRUE(gity::git::commitMatches("Fix turret crash", "Ahmet", "9527d02c", "turret"));
    EXPECT_TRUE(gity::git::commitMatches("Fix turret crash", "Ahmet", "9527d02c", "ahm"));
    EXPECT_TRUE(gity::git::commitMatches("Fix turret crash", "Ahmet", "9527d02c", "9527"));
}

TEST(CommitMatches, IsCaseInsensitive) {
    EXPECT_TRUE(gity::git::commitMatches("Fix Turret Crash", "Ahmet", "abc", "TURRET"));
    EXPECT_TRUE(gity::git::commitMatches("fix turret crash", "AHMET", "abc", "ahmet"));
}

TEST(CommitMatches, AnIdIsMatchedAsAPrefixNotAnywhere) {
    // A hex fragment appearing mid-hash is a coincidence. Matching it would
    // fill a search for "beef" with commits nobody was looking for.
    EXPECT_TRUE(gity::git::commitMatches("s", "a", "beef1234", "beef"));
    EXPECT_FALSE(gity::git::commitMatches("s", "a", "1234beef", "beef"));
}

TEST(CommitMatches, NonMatchesAreRejected) {
    EXPECT_FALSE(gity::git::commitMatches("Fix turret crash", "Ahmet", "9527d02c", "shader"));
}

TEST(CommitFilter, AuthorAndDateRangeNarrowTogether) {
    gity::git::CommitFilter filter;
    EXPECT_FALSE(filter.active());
    EXPECT_TRUE(filter.matches("anything", "Anyone", "abc", 100));

    filter.author = "ahmet";
    filter.since = 1000;
    filter.until = 2000;
    EXPECT_TRUE(filter.active());
    EXPECT_TRUE(filter.matches("Fix turret", "Ahmet Y", "abc", 1500));
    EXPECT_TRUE(filter.matches("Fix turret", "Ahmet Y", "abc", 1000)); // bounds are inclusive
    EXPECT_TRUE(filter.matches("Fix turret", "Ahmet Y", "abc", 2000));
    EXPECT_FALSE(filter.matches("Fix turret", "Ahmet Y", "abc", 999));
    EXPECT_FALSE(filter.matches("Fix turret", "Ahmet Y", "abc", 2001));
    // The author part looks at the author only: a summary naming Ahmet is not his commit.
    EXPECT_FALSE(filter.matches("Revert Ahmet's change", "Zeynep", "abc", 1500));

    filter.text = "turret";
    EXPECT_TRUE(filter.matches("Fix turret", "Ahmet Y", "abc", 1500));
    EXPECT_FALSE(filter.matches("Fix shader", "Ahmet Y", "abc", 1500));
}

TEST(CommitFilter, OpenEndedRanges) {
    gity::git::CommitFilter since;
    since.since = 1000;
    EXPECT_TRUE(since.matches("s", "a", "abc", 5000000000));
    EXPECT_FALSE(since.matches("s", "a", "abc", 10));
    gity::git::CommitFilter until;
    until.until = 1000;
    EXPECT_TRUE(until.matches("s", "a", "abc", 10));
    EXPECT_FALSE(until.matches("s", "a", "abc", 1001));
}
