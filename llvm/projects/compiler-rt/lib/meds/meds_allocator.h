//===-- meds_allocator.h ----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of Meds
//
// Meds-private header for meds_allocator.cc
//===----------------------------------------------------------------------===//

#ifndef MEDS_ALLOCATOR_H
#define MEDS_ALLOCATOR_H

#include "asan_flags.h"
#include "asan_internal.h"
#include "asan_interceptors.h"
#include "asan_allocator.h"
#include "sanitizer_common/sanitizer_aliasing.h"

namespace __meds {

void InitializeAllocator(const __asan::AllocatorOptions &options);
void ReInitializeAllocator(const __asan::AllocatorOptions &options);
void GetAllocatorOptions(__asan::AllocatorOptions *options);


const uptr kAllocatorSpace = ~(uptr)0;
const uptr kAllocatorSize  =  0x400000000000ULL;  // 64T.
typedef DefaultSizeClassMap SizeClassMap;

struct AP64 {
  static const uptr kSpaceBeg = kAllocatorSpace;
  static const uptr kSpaceSize = kAllocatorSize;
  static const uptr kMetadataSize = 0;
  typedef __meds::SizeClassMap SizeClassMap;
  typedef __asan::AsanMapUnmapCallback MapUnmapCallback;
};

typedef AliasingAllocator<AP64> PrimaryAllocator;
typedef LargeMmapAliasing<__asan::AsanMapUnmapCallback> SecondaryAllocator;
typedef AliasingAllocatorLocalCache<PrimaryAllocator> AliasingCache;
typedef CombinedAliasing<PrimaryAllocator, AliasingCache,
                         SecondaryAllocator> MedsAllocator;

struct MedsThreadLocalMallocStorage {
 AliasingCache aliasing_cache;
 private:
  MedsThreadLocalMallocStorage() {}
};

void InitCache(MedsThreadLocalMallocStorage *ms);
PrimaryAllocator getPrimary();


void *meds_memalign(uptr alignment, uptr size,
                    __asan::BufferedStackTrace *stack,
                    __asan::AllocType alloc_type);
void meds_free(void *ptr, __asan::BufferedStackTrace *stack,
               __asan::AllocType alloc_type);
void meds_sized_free(void *ptr, uptr size, __asan::BufferedStackTrace *stack,
                     __asan::AllocType alloc_type);

void *meds_malloc(uptr size, __asan::BufferedStackTrace *stack);
void *meds_calloc(uptr nmemb, uptr size, __asan::BufferedStackTrace *stack);
void *meds_realloc(void *p, uptr size, __asan::BufferedStackTrace *stack);
void *meds_valloc(uptr size, __asan::BufferedStackTrace *stack);
void *meds_pvalloc(uptr size, __asan::BufferedStackTrace *stack);

int meds_posix_memalign(void **memptr, uptr alignment, uptr size,
                        __asan::BufferedStackTrace *stack);
uptr meds_malloc_usable_size(const void *ptr, uptr pc, uptr bp);

uptr meds_mz_size(const void *ptr);

} // namespace __meds

#endif //  MEDS_ALLOCATOR_H
