//===-- asan_memory_profile.cc.cc -----------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of AddressSanitizer, an address sanity checker.
//
// This file implements __sanitizer_print_memory_profile.
//===----------------------------------------------------------------------===//

#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_stackdepot.h"
#include "sanitizer_common/sanitizer_stacktrace.h"
#include "sanitizer_common/sanitizer_stoptheworld.h"
#include "lsan/lsan_common.h"
#include "asan/asan_allocator.h"

