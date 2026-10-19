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

#include <set>
#include <string>

TEST(Result, EveryCodeHasItsOwnNonEmptyString) {
  std::set<std::string> seen;
  for (int r = 0; r < (int)GSHSCENE_RESULT_COUNT; r++) {
    const char * text = gshscene_result_string((GSHSCENE_Result)r);
    ASSERT_NE(text, nullptr) << r;
    EXPECT_GT(std::strlen(text), 0u) << r;
    EXPECT_TRUE(seen.insert(text).second) << "duplicate string for " << r << ": " << text;
  }
  EXPECT_STREQ(gshscene_result_string(GSHSCENE_OK), "ok");
}

TEST(Result, EveryCodeNamesItselfInWords) {
  EXPECT_STREQ(gshscene_result_string(GSHSCENE_OK), "ok");
  EXPECT_STREQ(gshscene_result_string(GSHSCENE_ERR_IO), "I/O error");
  EXPECT_STREQ(gshscene_result_string(GSHSCENE_ERR_FORMAT), "format error");
  EXPECT_STREQ(gshscene_result_string(GSHSCENE_ERR_UNSUPPORTED), "unsupported");
  EXPECT_STREQ(gshscene_result_string(GSHSCENE_ERR_LIMIT), "limit exceeded");
  EXPECT_STREQ(gshscene_result_string(GSHSCENE_ERR_CORRUPT), "corrupt");
  EXPECT_STREQ(gshscene_result_string(GSHSCENE_ERR_OOM), "out of memory");
  EXPECT_STREQ(gshscene_result_string(GSHSCENE_ERR_INVALID), "invalid argument");
  EXPECT_STREQ(gshscene_result_string(GSHSCENE_ERR_INTERNAL), "internal error");
}

TEST(Result, ACodeOutsideTheEnumHasAStringAndNoneOfTheRealOnes) {
  const char * unknown = gshscene_result_string(GSHSCENE_RESULT_COUNT);
  ASSERT_NE(unknown, nullptr);
  EXPECT_STREQ(unknown, "unknown result");
  EXPECT_STREQ(gshscene_result_string((GSHSCENE_Result)-1), "unknown result");
  EXPECT_STREQ(gshscene_result_string((GSHSCENE_Result)99), "unknown result");
}

TEST(Result, TheVocabularyIsTheSuitesFixedOneInItsOrder) {
  // CONVENTIONS section 5: the values are the order of the table.
  EXPECT_EQ((int)GSHSCENE_OK, 0);
  EXPECT_EQ((int)GSHSCENE_ERR_IO, 1);
  EXPECT_EQ((int)GSHSCENE_ERR_FORMAT, 2);
  EXPECT_EQ((int)GSHSCENE_ERR_UNSUPPORTED, 3);
  EXPECT_EQ((int)GSHSCENE_ERR_LIMIT, 4);
  EXPECT_EQ((int)GSHSCENE_ERR_CORRUPT, 5);
  EXPECT_EQ((int)GSHSCENE_ERR_OOM, 6);
  EXPECT_EQ((int)GSHSCENE_ERR_INVALID, 7);
  EXPECT_EQ((int)GSHSCENE_ERR_INTERNAL, 8);
  EXPECT_EQ((int)GSHSCENE_RESULT_COUNT, 9);
}

TEST(Limits, DefaultsAreTheDocumentedOnes) {
  GSHSCENE_Limits limits;
  std::memset(&limits, 0xAB, sizeof limits);
  gshscene_limits_default(&limits);
  EXPECT_EQ(limits.max_resources, (size_t)1 << 24);
  EXPECT_EQ(limits.max_resource_elements, (size_t)1 << 22);
  EXPECT_EQ(limits.max_list_items, (size_t)1 << 20);
  EXPECT_EQ(limits.max_list_bytes, (size_t)256 * 1024 * 1024);
  EXPECT_EQ(limits.max_depth, (size_t)256);
  EXPECT_EQ(limits.max_picture_depth, (size_t)32);
  EXPECT_EQ(limits.max_expansion, (size_t)1 << 24);
  for (unsigned char byte : limits._reserved) {
    EXPECT_EQ(byte, 0);
  }
  EXPECT_EQ(GSHSCENE_LIST_ITEMS_CEILING, (size_t)1 << 30);
}

TEST(Limits, DefaultIgnoresNull) {
  gshscene_limits_default(nullptr);
  SUCCEED();
}

TEST(Allocator, DefaultIsNeverNullAndAllocates) {
  const GSHSCENE_Allocator * a = gshscene_allocator_default();
  ASSERT_NE(a, nullptr);
  void * p = a->malloc_fn(a->ctx, 16);
  ASSERT_NE(p, nullptr);
  a->free_fn(a->ctx, p);
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
