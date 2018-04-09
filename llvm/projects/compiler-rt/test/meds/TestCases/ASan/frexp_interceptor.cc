// RUN: %clangxx_meds -O0 %s -o %t && not %run %t 2>&1 | FileCheck %s

// Test the frexp() interceptor.

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
int main() {
  double x = 3.14;
  int *exp = (int*)malloc(sizeof(int));
  free(exp);
  double y = frexp(x, exp);
  // CHECK: DEADLYSIGNAL
  // CHECK: SUMMARY
  return 0;
}
