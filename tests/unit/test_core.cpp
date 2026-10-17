/**
 * @file
 *
 * The version: the placeholder test the scaffold ships with.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include "test_helpers.h"

#include <ghoti.io/shape-scene/libver.h>

#include <cstring>

TEST(Version, StringMatchesTheGeneratedHeader) {
  EXPECT_STREQ(gshscene_version_string(), GSHSCENE_VERSION_STRING);
  EXPECT_EQ(gshscene_version_number(), GSHSCENE_VERSION_NUMBER);
  EXPECT_EQ(GSHSCENE_VERSION_NUMBER,
      GSHSCENE_MAKE_VERSION(GSHSCENE_VERSION_MAJOR, GSHSCENE_VERSION_MINOR,
          GSHSCENE_VERSION_PATCH));
}

TEST(Version, StringIsNeverNullOrEmpty) {
  ASSERT_NE(gshscene_version_string(), nullptr);
  EXPECT_GT(std::strlen(gshscene_version_string()), 0u);
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
