// FIXME: https://code.google.com/p/address-sanitizer/issues/detail?id=316
// XFAIL: android
//
// RUN: %clangxx_meds -DSHARED %s -shared -o %T/stack_trace_dlclose.so -fPIC
// RUN: %clangxx_meds -DSO_DIR=\"%T\" %s %libdl -o %t
// RUN: %env_asan_opts=exitcode=0 %run %t 2>&1 | FileCheck %s
// REQUIRES: stable-runtime

#include <assert.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include <sanitizer/common_interface_defs.h>

#ifdef SHARED
extern "C" {
void *foo() {
  return malloc(1);
}
}
#else
void *handle;

int main(int argc, char **argv) {
  void *handle = dlopen(SO_DIR "/stack_trace_dlclose.so", RTLD_LAZY);
  assert(handle);
  void *(*foo)() = (void *(*)())dlsym(handle, "foo");
  assert(foo);
  void *p = foo();
  assert(p);
  dlclose(handle);

  free(p);
  free(p);  // double-free

  return 0;
}
#endif

// CHECK: {{    #0 0x.* in (__interceptor_)?malloc}}
// CHECK: {{    #1 0x.* \(<unknown module>\)}}
// CHECK: {{    #2 0x.* in main}}
