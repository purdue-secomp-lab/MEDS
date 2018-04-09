// RUN: %clang_meds -O2 %s -o %t
// RUN: %env_asan_opts=strip_path_prefix='"%S/"' not %run %t 2>&1 | FileCheck %s

#include <stdlib.h>
int main() {
  char *x = (char*)malloc(10 * sizeof(char));
  free(x);
  return x[5];
  // Check that paths in error report don't start with slash.
  // CHECK: SEGV on unknown address
}
