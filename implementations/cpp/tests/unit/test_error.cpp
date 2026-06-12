// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
//
// Smoke test for the foundation API: osf::Error, osf::Result<T>,
// osf::version(). Designed to fail loudly if the basics break;
// behavioural tests for individual subsystems live in their own files.

#include <gtest/gtest.h>

#include <osf/osf.h>

namespace {

TEST(Error, ConstructibleFromCodeAndMessage) {
    osf::Error err{osf::Error::Code::ParseError, "bad header"};
    EXPECT_EQ(err.code, osf::Error::Code::ParseError);
    EXPECT_EQ(err.message, "bad header");
}

TEST(Result, HoldsValue) {
    osf::Result<int> r = 42;
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(*r, 42);
}

TEST(Result, HoldsError) {
    osf::Result<int> r = tl::make_unexpected(
        osf::Error{osf::Error::Code::IoError, "read failed"}
    );
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, osf::Error::Code::IoError);
    EXPECT_EQ(r.error().message, "read failed");
}

TEST(Version, NotEmpty) {
    EXPECT_FALSE(osf::version().empty());
    EXPECT_GE(OSF_VERSION_MAJOR, 0);
}

TEST(ErrorCategoryName, ReturnsKnownNames) {
    EXPECT_EQ(osf::error_category_name(osf::Error::Code::ParseError),
              "ParseError");
    EXPECT_EQ(osf::error_category_name(osf::Error::Code::Unknown),
              "Unknown");
}

}  // namespace
