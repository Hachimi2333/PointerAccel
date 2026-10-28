#include "common.hpp"
#include <gtest/gtest.h>
#include <cmath>
#include <limits>

TEST(Adapter, RejectsTransformedOrInvalidRawCoordinates) {
    EXPECT_FALSE(pa::rawMotion(0.5,1));
    EXPECT_FALSE(pa::rawMotion(INFINITY,1));
    EXPECT_FALSE(pa::rawMotion(2147483648.0,0));
    EXPECT_TRUE(pa::rawMotion(-2147483648.0,2147483647.0));
}
TEST(Adapter, ValidatesCoalescedReportsAndDetectsMissingOrDuplicatedInput) {
    PaReport reports[]={{2,3,1,2,100,"event1"},{4,-1,3,-2,101,"event2"}};
    EXPECT_TRUE(pa::batchMatches(reports,6,2,4,0,101,"event1"));
    EXPECT_FALSE(pa::batchMatches(reports,6,2,4,0,100,"event1"));
    EXPECT_FALSE(pa::batchMatches(reports,6,2,4,0,101,"event2"));
    EXPECT_FALSE(pa::batchMatches(reports,6,2,3,0,101,"event1"));
    EXPECT_FALSE(pa::batchMatches(std::span(reports).first(1),6,2,4,0,101,"event1"));
}
TEST(Adapter, PerReportEvaluationDiffersFromEvaluatingMergedInput) {
    windows_pointer::Engine separate,merged;
    windows_pointer::Motion sum{};
    for (int i=0;i<10;++i) { auto out=separate.apply({1,1},120); sum.x+=out.x;sum.y+=out.y; }
    EXPECT_NE(sum,merged.apply({10,10},120));
}
