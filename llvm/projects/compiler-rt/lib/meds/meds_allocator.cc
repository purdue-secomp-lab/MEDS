//===-- meds_allocator.cc -------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of Meds.
//
//===----------------------------------------------------------------------===//

#include "sanitizer_common/sanitizer_aliasing.h"
#include "sanitizer_common/sanitizer_posix.h"
#include "meds_allocator.h"
#include "asan_mapping.h"
#include "asan_thread.h"
#include "asan_internal.h"
#include "asan_stack.h"
#include "asan_report.h"
#include "asan_poisoning.h"

namespace __meds {

AliasingCache *GetAliasingCache(MedsThreadLocalMallocStorage *ms) {
  CHECK(ms);
  return &ms->aliasing_cache;
}


uptr ComputRZLog(uptr user_requested_size) {
  return 14;
}

uptr RZLog2Size(uptr rz_log) {
  return 1ULL << rz_log;
}

struct MedsMetaData {
  uptr user_requested_size;
  uptr alloc_beg;
};

static const uptr kMetaSize = sizeof(MedsMetaData);

struct Allocator {
  static const uptr kMaxAllowedMallocSize =
      FIRST_32_SECOND_64(3UL << 30, 1ULL << 40);
  MedsAllocator allocator;
  AliasingCache fallback_cache;
  StaticSpinMutex fallback_mutex;

  atomic_uint8_t alloc_dealloc_mismatch;
  atomic_uint16_t min_redzone;
  atomic_uint16_t max_redzone;

  explicit Allocator(LinkerInitialized) {}

  void Initialize(const __asan::AllocatorOptions &options) {
    allocator.Init(options.may_return_null, options.release_to_os_interval_ms);
    atomic_store(&alloc_dealloc_mismatch, options.alloc_dealloc_mismatch,
                 memory_order_release);
    atomic_store(&min_redzone, options.min_redzone, memory_order_release);
    atomic_store(&max_redzone, options.max_redzone, memory_order_release);
  }

  void UnmapShadow(uptr addr, uptr size) {
    uptr page_size = GetPageSizeCached();
    uptr shadow_beg = MEM_TO_SHADOW(addr);
    uptr shadow_end = MEM_TO_SHADOW(addr + size);
    uptr unmap_beg = RoundDownTo(shadow_beg, page_size);
    uptr unmap_end = RoundUpTo(shadow_end, page_size);
    internal_munmap((void *)unmap_beg, unmap_end - unmap_beg);
    MmapFixedOrDie(unmap_beg, unmap_end - unmap_beg);
  }

  void *Allocate(uptr size, uptr alignment, BufferedStackTrace *stack,
                 __asan::AllocType alloc_type, bool can_fill) {
    if (UNLIKELY(!__asan::asan_inited))
        __asan::AsanInitFromRtl();
    const uptr min_alignment = SHADOW_GRANULARITY;
    if (alignment < min_alignment)
      alignment = min_alignment;
    if (size == 0) {
      size = 1;
    }
    CHECK(IsPowerOfTwo(alignment));
    uptr rz_log = ComputRZLog(size);
    uptr rz_size = RZLog2Size(rz_log);
    uptr rounded_size = RoundUpTo(size, alignment);
    uptr needed_size = rounded_size + kMetaSize;
    if (alignment > min_alignment)
      needed_size += alignment;
    bool using_primary_allocator = true;
    if (!PrimaryAllocator::CanAllocate(needed_size, alignment)) {
      needed_size += rz_size * 2;
      using_primary_allocator = false;
    }
    CHECK(IsAligned(needed_size, min_alignment));
    if (size > kMaxAllowedMallocSize || needed_size > kMaxAllowedMallocSize) {
      Report("WARNING: AddressSanitizer failed to allocate 0x%zx bytes\n",
             (void*)size);
      return allocator.ReturnNullOrDieOnBadRequest();
    }

    __asan::AsanThread *t = __asan::GetCurrentThread();
    void *allocated;
    if (t) {
      AliasingCache *cache = GetAliasingCache(&t->meds_storage());
      allocated =
        allocator.Allocate(cache, needed_size, 8, rz_size, false, false);
    } else {
      SpinMutexLock l(&fallback_mutex);
      AliasingCache *cache = &fallback_cache;
      allocated =
        allocator.Allocate(cache, needed_size, 8, rz_size, false, false);
    }

    if (!allocated) return allocator.ReturnNullOrDieOnOOM();

    if (*(u8 *)MEM_TO_SHADOW((uptr)allocated) == 0 && __asan::CanPoisonMemory()) {
      uptr allocated_size = allocator.GetActuallyAllocatedSize(allocated);
      __asan::PoisonShadow((uptr)allocated, allocated_size,
                           __asan::kAsanHeapLeftRedzoneMagic);
    }

    uptr alloc_beg = reinterpret_cast<uptr>(allocated);
    uptr alloc_end = alloc_beg + needed_size;
    uptr beg_plus_meta = alloc_beg + kMetaSize;
    uptr user_beg = beg_plus_meta;
    if (!IsAligned(user_beg, alignment))
      user_beg = RoundUpTo(user_beg, alignment);
    uptr user_end = user_beg + size;
    CHECK_LE(user_end, alloc_end);
    uptr meta_beg = user_beg - kMetaSize;
    MedsMetaData *m = reinterpret_cast<MedsMetaData *>(meta_beg);
    m->user_requested_size = size;
    m->alloc_beg = alloc_beg;

    uptr size_rounded_down_to_granularity =
      RoundDownTo(size, SHADOW_GRANULARITY);
    if (size_rounded_down_to_granularity)
      __asan::PoisonShadow(user_beg, size_rounded_down_to_granularity, 0);
    if (size != size_rounded_down_to_granularity && __asan::CanPoisonMemory()) {
      u8 *shadow =
        (u8 *)__asan::MemToShadow(user_beg + size_rounded_down_to_granularity);
      *shadow = (size & (SHADOW_GRANULARITY - 1));
    }

    void *res = reinterpret_cast<void *>(user_beg);
    return res;
  }

  void Deallocate(void *ptr, uptr delete_size, BufferedStackTrace *stack,
                  __asan::AllocType alloc_type) {
    uptr p = reinterpret_cast<uptr>(ptr);
    if (p == 0) return;

    uptr meta_beg = p - kMetaSize;
    MedsMetaData *m = reinterpret_cast<MedsMetaData *>(meta_beg);
    __asan::AsanThread *t = __asan::GetCurrentThread();

    void *alloc_beg = reinterpret_cast<void *>(m->alloc_beg);
    uptr size = allocator.GetActuallyAllocatedSize(alloc_beg);
    UnmapShadow(m->alloc_beg, size);
    if (t) {
      AliasingCache *cache = GetAliasingCache(&t->meds_storage());
      allocator.Deallocate(cache, alloc_beg);
    } else {
      SpinMutexLock l(&fallback_mutex);
      AliasingCache *cache = &fallback_cache;
      allocator.Deallocate(cache, alloc_beg);
    }
  }

  void *Reallocate(void *old_ptr, uptr new_size, BufferedStackTrace *stack) {
    CHECK(old_ptr && new_size);
    uptr p = reinterpret_cast<uptr>(old_ptr);
    uptr meta_beg = p - kMetaSize;
    MedsMetaData *m = reinterpret_cast<MedsMetaData *>(meta_beg);

    void *new_ptr = Allocate(new_size, 8, stack, __asan::FROM_MALLOC, false);
    if (new_ptr) {
      CHECK_NE(REAL(memcpy), nullptr);
      uptr memcpy_size = Min(new_size, m->user_requested_size);
      REAL(memcpy)(new_ptr, old_ptr, memcpy_size);
      Deallocate(old_ptr, 0, stack, __asan::FROM_MALLOC);
    }
    return new_ptr;
  }

  void *Calloc(uptr nmemb, uptr size, BufferedStackTrace *stack) {
    if (CallocShouldReturnNullDueToOverflow(size, nmemb))
      return allocator.ReturnNullOrDieOnBadRequest();
    void *ptr = Allocate(nmemb * size, 8, stack, __asan::FROM_MALLOC, false);
    // If the memory comes from the secondary allocator no need to clear it
    // as it comes directly from mmap.
    if (ptr && allocator.FromPrimary(ptr))
      REAL(memset)(ptr, 0, nmemb * size);
    return ptr;
  }

  uptr AllocationSize(uptr p) {
    MedsMetaData *m = reinterpret_cast<MedsMetaData *>(p - kMetaSize);
    return m->user_requested_size;
  }

  void ForceLock() {
    allocator.ForceLock();
    fallback_mutex.Lock();
  }

  void ForceUnlock() {
    fallback_mutex.Unlock();
    allocator.ForceUnlock();
  }

  bool PointerIsMine(uptr addr) {
    return allocator.PointerIsMine(reinterpret_cast<void *>(addr));
  }

};

static Allocator instance(LINKER_INITIALIZED);

static MedsAllocator &get_allocator() {
  return instance.allocator;
}

void InitCache(MedsThreadLocalMallocStorage *ms) {
  get_allocator().InitCache(&ms->aliasing_cache); 
}

PrimaryAllocator getPrimary() {
  return get_allocator().GetPrimary();
}

void InitializeAllocator(const __asan::AllocatorOptions &options) {
  instance.Initialize(options);
}

void *meds_memalign(uptr alignment, uptr size, BufferedStackTrace *stack,
                   __asan::AllocType alloc_type) {
  return instance.Allocate(size, alignment, stack, alloc_type, true);
}

void meds_free(void *ptr, BufferedStackTrace *stack, __asan::AllocType alloc_type) {
  instance.Deallocate(ptr, 0, stack, alloc_type);
}

void meds_sized_free(void *ptr, uptr size, BufferedStackTrace *stack,
                    __asan::AllocType alloc_type) {
  instance.Deallocate(ptr, size, stack, alloc_type);
}

void *meds_malloc(uptr size, BufferedStackTrace *stack) {
  return instance.Allocate(size, 8, stack, __asan::FROM_MALLOC, true);
}

void *meds_calloc(uptr nmemb, uptr size, BufferedStackTrace *stack) {
  return instance.Calloc(nmemb, size, stack);
}

void *meds_realloc(void *p, uptr size, BufferedStackTrace *stack) {
  if (!p)
    return instance.Allocate(size, 8, stack, __asan::FROM_MALLOC, true);
  if (size == 0) {
    instance.Deallocate(p, 0, stack, __asan::FROM_MALLOC);
    return nullptr;
  }
  return instance.Reallocate(p, size, stack);
}

void *meds_valloc(uptr size, BufferedStackTrace *stack) {
  return instance.Allocate(size, GetPageSizeCached(), stack, __asan::FROM_MALLOC, true);
}

void *meds_pvalloc(uptr size, BufferedStackTrace *stack) {
  uptr PageSize = GetPageSizeCached();
  size = RoundUpTo(size, PageSize);
  if (size == 0) {
    // pvalloc(0) should allocate one page.
    size = PageSize;
  }
  return instance.Allocate(size, PageSize, stack, __asan::FROM_MALLOC, true);
}

int meds_posix_memalign(void **memptr, uptr alignment, uptr size,
                       BufferedStackTrace *stack) {
  void *ptr = instance.Allocate(size, alignment, stack, __asan::FROM_MALLOC, true);
  CHECK(IsAligned((uptr)ptr, alignment));
  *memptr = ptr;
  return 0;
}

uptr meds_malloc_usable_size(const void *ptr, uptr pc, uptr bp) {
  if (!ptr) return 0;
  uptr usable_size = instance.AllocationSize(reinterpret_cast<uptr>(ptr));
  if (__asan::flags()->check_malloc_usable_size && (usable_size == 0)) {
    __asan::BufferedStackTrace stack;
    __asan::GetStackTraceWithPcBpAndContext(&stack, kStackTraceMax,
                                            pc, bp, 0,
                                            common_flags()->fast_unwind_on_fatal);
    __asan::ReportMallocUsableSizeNotOwned((uptr)ptr, &stack);
  }
  return usable_size;
}

uptr meds_mz_size(const void *ptr) {
  return instance.AllocationSize(reinterpret_cast<uptr>(ptr));
}

void meds_mz_force_lock() {
  instance.ForceLock();
}

void meds_mz_force_unlock() {
  instance.ForceUnlock();
}

bool PointerIsMine(uptr addr) {
  return instance.PointerIsMine(addr);
}

} // namespace __meds
