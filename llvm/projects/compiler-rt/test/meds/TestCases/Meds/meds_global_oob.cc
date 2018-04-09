// RUN: %clangxx_meds_global -O0 %s -o %t && not %run %t 2>&1 | FileCheck %s

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

char a[10];
char b[10];
char c[10];
char d[10];
char e[10];
char f[10];
char g[10];
char h[10];
char i[10];

int main(int argc, char **argv) {
  a[argc * 64] = 'a';
  // CHECK: {{AddressSanitizer}}
  return 0;
}
