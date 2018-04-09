// Test that mixed static/dynamic sanitization of program objects
// is prohibited.
//
// RUN: %clangxx_meds -DBUILD_SO=1 -fPIC -shared %s -o %t.so
// RUN: %clangxx_meds_static %s %t.so -o %t
// RUN: not %run %t 2>&1 | FileCheck %s

// REQUIRES: asan-dynamic-runtime
// XFAIL: android

#if BUILD_SO
char dummy;
void do_access(const void *p) { dummy = ((const char *)p)[1]; }
#else
#include <stdlib.h>
extern void do_access(const void *p);
int main(int argc, char **argv) {
  void *p = malloc(1);
  do_access(p);
  free(p);
  return 0;
}
#endif

// CHECK: Your application is linked against incompatible ASan runtimes
