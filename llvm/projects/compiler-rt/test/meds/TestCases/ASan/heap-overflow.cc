// RUN: %clangxx_meds -O0 %s -o %t && not %run %t 2>&1 | FileCheck %s
// RUN: %clangxx_meds -O1 %s -o %t && not %run %t 2>&1 | FileCheck %s
// RUN: %clangxx_meds -O2 %s -o %t && not %run %t 2>&1 | FileCheck %s
// RUN: %clangxx_meds -O3 %s -o %t && not %run %t 2>&1 | FileCheck %s
// RUN: %env_asan_opts=print_stats=1 not %run %t 2>&1 | FileCheck %s

// FIXME: Fix this test under GCC.
// REQUIRES: Clang

#include <stdlib.h>
#include <string.h>
int main(int argc, char **argv) {
  char *x = (char*)malloc(10 * sizeof(char));
  memset(x, 0, 10);
  int res = x[argc * 10];  // BOOOM
  // CHECK: ERROR
  free(x);
  return res;
}
