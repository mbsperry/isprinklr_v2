// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

#include <unity.h>

#include "cors.h"

void setUp() {}
void tearDown() {}

namespace {

void test_allows_lan_origins() {
  TEST_ASSERT_TRUE(cors::isAllowedOrigin("http://192.168.1.20:8080"));
  TEST_ASSERT_TRUE(cors::isAllowedOrigin("http://10.0.0.5"));
  TEST_ASSERT_TRUE(cors::isAllowedOrigin("http://172.16.0.1:3000"));
  TEST_ASSERT_TRUE(cors::isAllowedOrigin("http://172.31.255.255"));
  TEST_ASSERT_TRUE(cors::isAllowedOrigin("http://169.254.3.4"));
  TEST_ASSERT_TRUE(cors::isAllowedOrigin("http://localhost:5173"));
  TEST_ASSERT_TRUE(cors::isAllowedOrigin("http://127.0.0.1:5173"));
  TEST_ASSERT_TRUE(cors::isAllowedOrigin("http://isprinklr:8080"));
  TEST_ASSERT_TRUE(cors::isAllowedOrigin("http://sprinkler.local"));
  TEST_ASSERT_TRUE(cors::isAllowedOrigin("https://ct.home.arpa"));
  TEST_ASSERT_TRUE(cors::isAllowedOrigin("http://Router.LAN"));
}

void test_refuses_public_and_malformed_origins() {
  TEST_ASSERT_FALSE(cors::isAllowedOrigin("https://example.com"));
  TEST_ASSERT_FALSE(cors::isAllowedOrigin("http://8.8.8.8"));
  TEST_ASSERT_FALSE(cors::isAllowedOrigin("http://172.32.0.1"));
  TEST_ASSERT_FALSE(cors::isAllowedOrigin("http://192.169.0.1"));
  TEST_ASSERT_FALSE(cors::isAllowedOrigin("http://evil.local.example.com"));
}

void test_refuses_odd_shapes() {
  TEST_ASSERT_FALSE(cors::isAllowedOrigin(""));
  TEST_ASSERT_FALSE(cors::isAllowedOrigin("null"));
  TEST_ASSERT_FALSE(cors::isAllowedOrigin("file://"));
  TEST_ASSERT_FALSE(cors::isAllowedOrigin("http://"));
  TEST_ASSERT_FALSE(cors::isAllowedOrigin("http://192.168.1.2/path"));
  TEST_ASSERT_FALSE(cors::isAllowedOrigin("http://user@192.168.1.2"));
  TEST_ASSERT_FALSE(cors::isAllowedOrigin("http://192.168.1.2:99999"));
  TEST_ASSERT_FALSE(cors::isAllowedOrigin("http://192.168.1.256"));
  TEST_ASSERT_FALSE(cors::isAllowedOrigin("http://192.168.1"));
  TEST_ASSERT_FALSE(cors::isAllowedOrigin("http://exa_mple.local"));
}

}  // namespace

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_allows_lan_origins);
  RUN_TEST(test_refuses_public_and_malformed_origins);
  RUN_TEST(test_refuses_odd_shapes);
  return UNITY_END();
}
