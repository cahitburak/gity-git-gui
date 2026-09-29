#include "core/git/RebaseTodo.h"

#include <gtest/gtest.h>

using gity::git::RebaseAction;
using gity::git::RebaseStep;
using gity::git::serializeTodo;
using gity::git::TodoProblem;
using gity::git::validateTodo;

namespace {
RebaseStep step(RebaseAction action, const char* oid, const char* subject = "") {
    return {action, oid, subject, {}, {}};
}
} // namespace

TEST(SerializeTodo, ReversesIntoTheOrderGitApplies) {
    // The list arrives newest first, as history is read. git runs the file top
    // to bottom starting from the oldest commit, so handing it over unreversed
    // rebases the commits backwards and produces a history that looks
    // plausible and is wrong.
    const std::string todo = serializeTodo({
        step(RebaseAction::Pick, "cccccc", "newest"),
        step(RebaseAction::Pick, "bbbbbb", "middle"),
        step(RebaseAction::Pick, "aaaaaa", "oldest"),
    });
    EXPECT_EQ(todo,
              "pick aaaaaa oldest\n"
              "pick bbbbbb middle\n"
              "pick cccccc newest\n");
}

TEST(SerializeTodo, WritesGitsKeywordForEachAction) {
    const std::string todo = serializeTodo({
        step(RebaseAction::Drop, "444444"),
        step(RebaseAction::Fixup, "333333"),
        step(RebaseAction::Squash, "222222"),
        step(RebaseAction::Edit, "111111"),
    });
    EXPECT_EQ(todo,
              "edit 111111\n"
              "squash 222222\n"
              "fixup 333333\n"
              "drop 444444\n");
}

TEST(SerializeTodo, ASubjectIsOptional) {
    EXPECT_EQ(serializeTodo({step(RebaseAction::Pick, "abc123")}), "pick abc123\n");
}

TEST(ValidateTodo, AnOrdinaryListIsFine) {
    EXPECT_EQ(validateTodo({
                  step(RebaseAction::Fixup, "cccccc"),
                  step(RebaseAction::Pick, "bbbbbb"),
                  step(RebaseAction::Pick, "aaaaaa"),
              }),
              TodoProblem::None);
}

TEST(ValidateTodo, TheOldestKeptStepCannotFold) {
    // "aaaaaa" is oldest and runs first; there is nothing before it in this
    // rebase to fold into. git aborts partway through for this, leaving the
    // repository mid-rebase over a mistake that was visible beforehand.
    EXPECT_EQ(validateTodo({
                  step(RebaseAction::Pick, "bbbbbb"),
                  step(RebaseAction::Squash, "aaaaaa"),
              }),
              TodoProblem::LeadingFold);
}

TEST(ValidateTodo, DroppedStepsDoNotCountAsSomethingToFoldInto) {
    // The oldest step is dropped, so the fixup above it becomes the first
    // thing git runs — and still has nothing to fold into. Checking only
    // steps[last] would miss this.
    EXPECT_EQ(validateTodo({
                  step(RebaseAction::Pick, "cccccc"),
                  step(RebaseAction::Fixup, "bbbbbb"),
                  step(RebaseAction::Drop, "aaaaaa"),
              }),
              TodoProblem::LeadingFold);
}

TEST(ValidateTodo, AFoldIsFineOnceSomethingKeptPrecedesIt) {
    EXPECT_EQ(validateTodo({
                  step(RebaseAction::Fixup, "cccccc"),
                  step(RebaseAction::Drop, "bbbbbb"),
                  step(RebaseAction::Pick, "aaaaaa"),
              }),
              TodoProblem::None);
}

TEST(ValidateTodo, DroppingEverythingIsFlaggedRatherThanPerformed) {
    // git accepts this and it means "delete all of this work".
    EXPECT_EQ(validateTodo({
                  step(RebaseAction::Drop, "bbbbbb"),
                  step(RebaseAction::Drop, "aaaaaa"),
              }),
              TodoProblem::EverythingDropped);
}

TEST(ValidateTodo, AnEmptyListIsRejected) {
    EXPECT_EQ(validateTodo({}), TodoProblem::Empty);
}

TEST(SerializeTodo, ARewordIsAPickFollowedByAnAmendOfTheMessage) {
    RebaseStep reword = step(RebaseAction::Reword, "bbbbbb");
    reword.messageFile = "/repo/.git/gity/reword/bbbbbb";
    EXPECT_EQ(serializeTodo({reword, step(RebaseAction::Pick, "aaaaaa")}),
              "pick aaaaaa\n"
              "pick bbbbbb\n"
              "exec git commit --amend --only --allow-empty --cleanup=strip "
              "--file='/repo/.git/gity/reword/bbbbbb'\n");
}

TEST(SerializeTodo, AMessageFilePathIsQuotedForTheShell) {
    RebaseStep reword = step(RebaseAction::Reword, "aaaaaa");
    reword.messageFile = "/it's here/msg";
    EXPECT_EQ(serializeTodo({reword}),
              "pick aaaaaa\n"
              "exec git commit --amend --only --allow-empty --cleanup=strip "
              "--file='/it'\\''s here/msg'\n");
}

TEST(ValidateTodo, ARewordCountsAsKept) {
    EXPECT_EQ(validateTodo({step(RebaseAction::Fixup, "bbbbbb"),
                            step(RebaseAction::Reword, "aaaaaa")}),
              TodoProblem::None);
}
