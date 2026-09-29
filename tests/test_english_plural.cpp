#include "core/model/EnglishPlural.h"

#include <gtest/gtest.h>

using gity::model::englishPlural;

TEST(EnglishPlural, ResolvesEveryMarker) {
    EXPECT_EQ(englishPlural("%n file(s)", 1), "%n file");
    EXPECT_EQ(englishPlural("%n file(s)", 3), "%n files");
    EXPECT_EQ(englishPlural("%n file(s)", 0), "%n files");
    EXPECT_EQ(englishPlural("Stage %n file(s) in %n commit(s)", 2), "Stage %n files in %n commits");
    EXPECT_EQ(englishPlural("no marker", 5), "no marker");
}
