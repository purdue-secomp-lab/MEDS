// Test that on non-glibc platforms, a number of malloc-related functions are
// not intercepted.

// RUN: not %clang_meds -Dtestfunc=mallinfo %s -o %t
// RUN: not %clang_meds -Dtestfunc=mallopt  %s -o %t
// RUN: not %clang_meds -Dtestfunc=memalign %s -o %t
// RUN: not %clang_meds -Dtestfunc=pvalloc  %s -o %t
// RUN: not %clang_meds -Dtestfunc=cfree    %s -o %t

#include <stdlib.h>

// For glibc, cause link failures by referencing a nonexistent function.
#ifdef __GLIBC__
#undef testfunc
#define testfunc nonexistent_function
#endif

void testfunc(void);

int main(void)
{
  testfunc();
  return 0;
}
