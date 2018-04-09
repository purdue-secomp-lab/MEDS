// RUN: %clangxx_meds -O0 %s -o %t && not %run %t 2>&1 | FileCheck %s
// RUN: %clangxx_meds -O1 %s -o %t && not %run %t 2>&1 | FileCheck %s
// RUN: %clangxx_meds -O2 %s -o %t && not %run %t 2>&1 | FileCheck %s
// RUN: %clangxx_meds -O3 %s -o %t && not %run %t 2>&1 | FileCheck %s

#include <stdlib.h>
int main(int argc, char **argv) {
  volatile int *x = (int*)malloc(2*sizeof(int) + 2);
  int res = x[2];  // BOOOM
  // CHECK: ERROR
  return res;
}
