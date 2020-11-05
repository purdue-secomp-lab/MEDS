MEDS: Enhancing Memory Error Detection for Large-Scale Applications
===================================================================

# Prerequisites
- `cmake` and `clang`

# Build MEDS supporting compiler

```console
$ make
```

# Build Using Docker

```console
# build docker image
$ docker build -t meds .

# run docker image
$ docker run --cap-add=SYS_PTRACE -it meds /bin/bash
```

# Testing MEDS
- MEDS's testing runs original ASAN's testcases as well as MEDS
  specific testcases.
    - Copied ASAN's testcases in `llvm/projects/compiler-rt/test/meds/TestCases/ASan`
    - MEDS specific testcases in `llvm/projects/compiler-rt/test/meds/TestCases/Meds`

- To run the test,

```console
$ make test

Testing Time: 30.70s
 Expected Passes    : 183
 Expected Failures  : 1
 Unsupported Tests  : 50
```

# Build applications with MEDS heap allocation and ASan stack and global

- Given a test program `test.cc`,

```console
$ cat > test.cc

int main(int argc, char **argv) {
  int *a = new int[10];
  a[argc * 10] = 1;
  return 0;
}
```

- `test.cc` can be built using the option, `-fsanitize=meds`.

```console
$ build/bin/clang++ -fsanitize=meds test.cc -o test
$ ./test

==90589==ERROR: AddressSanitizer: heap-buffer-overflow on address 0x43fff67eb078 at pc 0x0000004f926d bp 0x7fffffffe440 sp 0x7fffffffe438
WRITE of size 4 at 0x43fff67eb078 thread T0
    #0 0x4f926c in main (/home/wookhyun/release/meds-release/a.out+0x4f926c)
    #1 0x7ffff6b5c82f in __libc_start_main /build/glibc-bfm8X4/glibc-2.23/csu/../csu/libc-start.c:291
    #2 0x419cb8 in _start (/home/wookhyun/release/meds-release/a.out+0x419cb8)

Address 0x43fff67eb078 is a wild pointer.
SUMMARY: AddressSanitizer: heap-buffer-overflow (/home/wookhyun/release/meds-release/a.out+0x4f926c) in main
Shadow bytes around the buggy address:
  0x08807ecf55b0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
  0x08807ecf55c0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
  0x08807ecf55d0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
  0x08807ecf55e0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
  0x08807ecf55f0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
=>0x08807ecf5600: fa fa fa fa fa fa fa fa fa fa 00 00 00 00 00[fa]
  0x08807ecf5610: fa fa fa fa fa fa fa fa fa fa fa fa fa fa fa fa
  0x08807ecf5620: fa fa fa fa fa fa fa fa fa fa fa fa fa fa fa fa
  0x08807ecf5630: fa fa fa fa fa fa fa fa fa fa fa fa fa fa fa fa
  0x08807ecf5640: fa fa fa fa fa fa fa fa fa fa fa fa fa fa fa fa
  0x08807ecf5650: fa fa fa fa fa fa fa fa fa fa fa fa fa fa fa fa
Shadow byte legend (one shadow byte represents 8 application bytes):
  Addressable:           00
  Partially addressable: 01 02 03 04 05 06 07
  Heap left redzone:       fa
  Freed heap region:       fd
  Stack left redzone:      f1
  Stack mid redzone:       f2
  Stack right redzone:     f3
  Stack after return:      f5
  Stack use after scope:   f8
  Global redzone:          f9
  Global init order:       f6
  Poisoned by user:        f7
  Container overflow:      fc
  Array cookie:            ac
  Intra object redzone:    bb
  ASan internal:           fe
  Left alloca redzone:     ca
  Right alloca redzone:    cb
==90589==ABORTING
```

# Options

- `-fsanitize=meds`: Enable heap protection using MEDS (stack and
  global are protected using ASAN)
- `-mllvm -meds-stack=1`: Enable stack protection using MEDS
- `-mllvm -meds-global=1 -mcmodel=large`: Enable global protection using MEDS
    - This also requires `--emit-relocs` in `LDFLAGS`

- Example: to protect heap/stack using MEDS and global using ASAN

```console
$ clang -fsanitize=meds -mllvm -meds-stack=1 test.c -o test
```

- Example: to protect heap/global using MEDS and stack using ASAN

```console
$ clang -fsanitize=meds -mllvm -meds-global=1 -mcmodel=large -Wl,-emit-relocs test.c -o test
```

- Example: to protect heap/stack/global using MEDS
```console
$ clang -fsanitize=meds -mllvm -meds-stack=1 -mllvm -meds-global=1 -mcmodel=large -Wl,--emit-relocs
```

# Contributors
- Wookhyun Han (wookhyunhan@gmail.com)
- Byunggil Joe (cp4419@kaist.ac.kr)
- Byoungyougn Lee (byoungyoung@purdue.edu)
- Chengyu Song (csong@cs.ucr.edu)
- Insik Shin (insik.shin@cs.kaist.ac.kr)
