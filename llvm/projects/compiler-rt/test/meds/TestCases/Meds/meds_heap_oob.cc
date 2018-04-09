// RUN: %clangxx_meds -O0 %s -o %t && not %run %t 2>&1 | FileCheck %s
// RUN: %clangxx_meds -O1 %s -o %t && not %run %t 2>&1 | FileCheck %s
// RUN: %clangxx_meds -O2 %s -o %t && not %run %t 2>&1 | FileCheck %s
// RUN: %clangxx_meds -O3 %s -o %t && not %run %t 2>&1 | FileCheck %s

#include <string.h>
#include <stdlib.h>

int main(int argc, char **argv) {
  char *a[100];
  for(int i = 0; i < 100; i++ ) {
    a[i] = (char *)malloc(10);
  }
  a[0][argc * 100] = 'a';
  // CHECK: {{AddressSanitizer}}
  for(int i = 0; i < 100; i++) {
    free(a[i]);
  }
  return 0;
}
