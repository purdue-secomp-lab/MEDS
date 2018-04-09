// RUN: %clangxx_meds_stack -O0 %s -o %t && not %run %t 2>&1 | FileCheck %s

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

int main(int argc, char **argv) {
  char a[10];
  char b[10];
  char c[10];
  char d[10];
  char e[10];
  char f[10];
  char g[10];
  char h[10];
  char i[10];

  a[argc * 32] = 'a';
  // CHECK: {{AddressSanitizer}}
  return 0;
}
