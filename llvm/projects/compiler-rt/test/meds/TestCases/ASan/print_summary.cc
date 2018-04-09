// RUN: %clangxx_meds -O0 %s -o %t
// RUN: not %run %t 2>&1 | FileCheck %s --check-prefix=SOURCE
// RUN: %env_asan_opts=symbolize=false not %run %t 2>&1 | FileCheck %s --check-prefix=MODULE
// RUN: %env_asan_opts=print_summary=false not %run %t 2>&1 | FileCheck %s --check-prefix=MISSING

int main() {
  char *x = new char[20];
  delete[] x;
  return x[0];
  // SOURCE: ERROR: AddressSanitizer: SEGV on unknown address
  // MODULE: ERROR: AddressSanitizer: SEGV on unknown address
  // MISSING: ERROR: AddressSanitizer: SEGV on unknown address
  // MISSING-NOT: SUMMARY
}
