// Test string s1 overflow in strcspn function
// RUN: %clang_meds %s -o %t && %env_asan_opts=strict_string_checks=true not %run %t 2>&1 | FileCheck %s

// Test intercept_strspn asan option
// RUN: %env_asan_opts=intercept_strspn=false %run %t 2>&1

#include <assert.h>
#include <string.h>
#include <sanitizer/asan_interface.h>

int main(int argc, char **argv) {
  size_t r;
  char s2[] = "ab";
  char s1[4] = "caB";
  __asan_poison_memory_region ((char *)&s1[2], 2);
  r = strcspn(s1, s2);
  // CHECK:'s1' <== Memory access at offset {{[0-9]+}} partially overflows this variable
  assert(r == 1);
  return 0;
}
