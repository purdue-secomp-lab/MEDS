// RUN: %clangxx_meds -O0 %s -o %t && not %run %t 2>&1 | FileCheck %s

// Test the time() interceptor.

// There's no interceptor for time() on Windows yet.
// XFAIL: win32

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int main() {
  time_t *tm = (time_t*)malloc(sizeof(time_t));
  free(tm);
  time_t t = time(tm);
  printf("Time: %s\n", ctime(&t));  // NOLINT
  // CHECK: SEGV on unknown address
  // Regression check for
  // https://code.google.com/p/address-sanitizer/issues/detail?id=321
  // CHECK: SUMMARY
  return 0;
}
