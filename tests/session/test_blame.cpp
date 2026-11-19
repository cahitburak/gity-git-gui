// Blame through a session: real git, real porcelain, read by the parser.
#include "TempRepo.h"

#include "session/RepoSession.h"

#include <gtest/gtest.h>

#include <QEventLoop>
#include <QTimer>

using gity::session::BlamePtr;
using gity::session::RepoSession;
using gity::test::TempRepo;

namespace {

struct Answer {
    BlamePtr blame;
    QString problem;
    bool arrived = false;
};

/// Opens `repo`, asks for a blame, and waits for it.
Answer blame(const TempRepo& repo, const QString& path, const QString& revision) {
    RepoSession session;
    Answer answer;
    QEventLoop loop;
    QObject::connect(&session, &RepoSession::blameReady, &loop,
                     [&](const QString&, const QString&, BlamePtr result, const QString& problem) {
                         answer = {std::move(result), problem, true};
                         loop.quit();
                     });
    QTimer::singleShot(20000, &loop, &QEventLoop::quit);
    session.open(repo.dir());
    session.requestBlame(path, revision);
    loop.exec();
    return answer;
}

} // namespace

TEST(Blame, NamesTheCommitBehindEachLine) {
    TempRepo repo;
    repo.write("f.txt", "one\ntwo\n");
    repo.commitAll(QStringLiteral("first"));
    const QString first = repo.head();
    repo.write("f.txt", "one\nTWO\nthree\n");
    repo.commitAll(QStringLiteral("second"));

    const Answer answer = blame(repo, QStringLiteral("f.txt"), repo.head());
    ASSERT_TRUE(answer.arrived);
    ASSERT_TRUE(answer.blame) << answer.problem.toStdString();
    const auto& lines = answer.blame->lines;
    ASSERT_EQ(lines.size(), 3u);
    const auto& commits = answer.blame->commits;
    EXPECT_EQ(commits[lines[0].commit].id, first.toStdString());
    EXPECT_EQ(commits[lines[0].commit].summary, "first");
    EXPECT_EQ(commits[lines[1].commit].summary, "second");
    EXPECT_EQ(commits[lines[1].commit].previousId, first.toStdString());
    EXPECT_EQ(lines[1].text, "TWO");
    EXPECT_EQ(commits[lines[2].commit].summary, "second");

    // As of the first commit, the file it had then.
    const Answer before = blame(repo, QStringLiteral("f.txt"), first);
    ASSERT_TRUE(before.blame);
    ASSERT_EQ(before.blame->lines.size(), 2u);
    EXPECT_EQ(before.blame->lines[1].text, "two");
}

TEST(Blame, TheWorkingCopyShowsEditsAsNotCommitted) {
    TempRepo repo;
    repo.write("f.txt", "one\ntwo\n");
    repo.commitAll();
    repo.write("f.txt", "one\nedited\n");

    const Answer answer = blame(repo, QStringLiteral("f.txt"), {});
    ASSERT_TRUE(answer.blame) << answer.problem.toStdString();
    ASSERT_EQ(answer.blame->lines.size(), 2u);
    const auto& commits = answer.blame->commits;
    EXPECT_FALSE(commits[answer.blame->lines[0].commit].uncommitted());
    EXPECT_TRUE(commits[answer.blame->lines[1].commit].uncommitted());
}

TEST(Blame, SaysWhyWhenThereIsNothingToBlame) {
    TempRepo repo;
    repo.write("f.txt", "one\n");
    repo.commitAll();

    const Answer missing = blame(repo, QStringLiteral("nowhere.txt"), repo.head());
    ASSERT_TRUE(missing.arrived);
    EXPECT_FALSE(missing.blame);
    EXPECT_FALSE(missing.problem.isEmpty());

    // Anything but a commit id is refused before git sees it.
    const Answer odd = blame(repo, QStringLiteral("f.txt"), QStringLiteral("--output=x"));
    EXPECT_FALSE(odd.blame);
    EXPECT_TRUE(odd.problem.contains(QStringLiteral("--output=x")));
}

TEST(Blame, NamesAFileOutsideAsciiAsItIsSpelled) {
    // git quotes such names in porcelain unless told not to: "café.txt"
    // came back as "caf\303\251.txt", in quotes.
    TempRepo repo;
    const QString before = QStringLiteral("café.txt");
    const QString after = QStringLiteral("déjà.txt");
    repo.write(before, "one\n");
    repo.commitAll();
    repo.git({"mv", before, after});
    repo.write(after, "one\ntwo\n");
    repo.commitAll(QStringLiteral("renamed"));

    const Answer answer = blame(repo, after, repo.head());
    ASSERT_TRUE(answer.blame) << answer.problem.toStdString();
    ASSERT_EQ(answer.blame->lines.size(), 2u);
    const auto& commits = answer.blame->commits;
    EXPECT_EQ(commits[answer.blame->lines[0].commit].path, before.toStdString());
    EXPECT_EQ(commits[answer.blame->lines[1].commit].path, after.toStdString());
}
