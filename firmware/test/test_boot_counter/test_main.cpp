#include <unity.h>

#include "boot_counter.h"
#include "memory_kv_store.h"

using Decision = BootCounter::Decision;

void setUp() {}
void tearDown() {}

namespace {

uint32_t stored(MemoryKvStore& s) {
  uint32_t v = 12345;
  return s.getU32("bootguard", "fails", &v) ? v : 0xFFFFFFFFu;
}

}  // namespace

void test_first_boot_counts_one() {
  MemoryKvStore store;
  BootCounter bc(store, 3);
  TEST_ASSERT_EQUAL(Decision::kContinue, bc.onBoot());
  TEST_ASSERT_EQUAL_UINT32(0, bc.failedBoots());
  TEST_ASSERT_EQUAL_UINT32(1, stored(store));
}

void test_healthy_clears_count() {
  MemoryKvStore store;
  BootCounter bc(store, 3);
  bc.onBoot();
  bc.markHealthy();
  TEST_ASSERT_TRUE(bc.healthy());
  TEST_ASSERT_EQUAL_UINT32(0, stored(store));
}

void test_reverts_after_threshold_unhealthy_boots() {
  MemoryKvStore store;
  for (int i = 0; i < 3; ++i) {
    BootCounter bc(store, 3);
    TEST_ASSERT_EQUAL(Decision::kContinue, bc.onBoot());
    TEST_ASSERT_EQUAL_UINT32(i, bc.failedBoots());
  }
  BootCounter bc(store, 3);
  TEST_ASSERT_EQUAL(Decision::kRevert, bc.onBoot());
  // Cleared, so an impossible revert cannot loop.
  TEST_ASSERT_EQUAL_UINT32(0, stored(store));
  BootCounter next(store, 3);
  TEST_ASSERT_EQUAL(Decision::kContinue, next.onBoot());
}

void test_healthy_boot_resets_streak() {
  MemoryKvStore store;
  { BootCounter bc(store, 3); bc.onBoot(); }
  { BootCounter bc(store, 3); bc.onBoot(); bc.markHealthy(); }
  BootCounter bc(store, 3);
  bc.onBoot();
  TEST_ASSERT_EQUAL_UINT32(0, bc.failedBoots());
}

void test_reads_phase0_value() {
  // Phase 0 wrote the same namespace and key; an in-flight count carries over.
  MemoryKvStore store;
  store.putU32("bootguard", "fails", 2);
  BootCounter bc(store, 3);
  TEST_ASSERT_EQUAL(Decision::kContinue, bc.onBoot());
  TEST_ASSERT_EQUAL_UINT32(2, bc.failedBoots());
  TEST_ASSERT_EQUAL_UINT32(3, stored(store));
}

void test_store_failure_is_reported() {
  MemoryKvStore store;
  store.failWrites = true;
  BootCounter bc(store, 3);
  TEST_ASSERT_EQUAL(Decision::kContinue, bc.onBoot());
  TEST_ASSERT_TRUE(bc.storeFailed());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_first_boot_counts_one);
  RUN_TEST(test_healthy_clears_count);
  RUN_TEST(test_reverts_after_threshold_unhealthy_boots);
  RUN_TEST(test_healthy_boot_resets_streak);
  RUN_TEST(test_reads_phase0_value);
  RUN_TEST(test_store_failure_is_reported);
  return UNITY_END();
}
