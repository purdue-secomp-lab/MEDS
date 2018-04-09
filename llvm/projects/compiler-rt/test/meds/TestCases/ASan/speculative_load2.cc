// Verifies that speculative loads from unions do not happen under asan.
// RUN: %clangxx_meds -O0 %s -o %t && %run %t 2>&1
// RUN: %clangxx_meds -O1 %s -o %t && %run %t 2>&1
// RUN: %clangxx_meds -O2 %s -o %t && %run %t 2>&1
// RUN: %clangxx_meds -O3 %s -o %t && %run %t 2>&1

typedef union {
  short q;
  struct {
    short x;
    short y;
    int for_alignment;
  } w;
} U;

int main() {
  char *buf = new char[2];
  buf[0] = buf[1] = 0x0;
  U *u = (U *)buf;
  short result = u->q == 0 ? 0 : u->w.y;
  delete[] buf;
  return result;
}

