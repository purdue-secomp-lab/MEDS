//===-- sanitizer_aliasing.h ------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Specialized memory allocator for Meds.
//
//===----------------------------------------------------------------------===//

#ifndef SANITIZER_ALIASING_H
#define SANITIZER_ALIASING_H

#include "sanitizer_allocator.h"
#include "sanitizer_allocator_internal.h"
#include "sanitizer_posix.h"
#include "sanitizer_addrhashmap.h"
#include "sanitizer_doublelist.h"
#include "sanitizer_common/sanitizer_placement_new.h"
#include <sys/mman.h>

namespace __sanitizer {

template<class AliasingAllocator> struct AliasingAllocatorLocalCache;

struct Vpage {
  uptr addr;
  struct Vpage *prev;
  struct Vpage *next;
};

struct Ppage {
  DoubleList<Vpage> vlist;
  BlockingMutex mutex;
};

struct Freed {
  struct Freed *prev;
  struct Freed *next;
  struct Ppage *page;
  uptr offset;
  uptr size;
};

typedef AddrHashMap<Vpage *, 300007> Vmap;
typedef AddrHashMap<Ppage *, 300007> Pmap;

template <class Params>
class AliasingAllocator {
 public:
  Vmap *Forkmap;
  Pmap *Pagemap;
  static const uptr kSpaceBeg = Params::kSpaceBeg;
  static const uptr kSpaceSize = Params::kSpaceSize;
  static const uptr kMetadataSize = Params::kMetadataSize;

  typedef typename Params::SizeClassMap SizeClassMap;
  typedef typename Params::MapUnmapCallback MapUnmapCallback;

  typedef AliasingAllocator<Params> ThisT;
  typedef AliasingAllocatorLocalCache<ThisT> AllocatorCache;

  // When we know the size class (the region base) we can represent a pointer
  // as a 4-byte integer (offset from the region start shifted right by 4).

  void Init(s32 release_to_os_interval_ms) {
    uptr TotalSpaceSize = kSpaceSize + AdditionalSize();
    if (kUsingConstantSpaceBeg) {
      CHECK_EQ(kSpaceBeg, reinterpret_cast<uptr>(
                            MmapFixedNoAccess(kSpaceBeg, TotalSpaceSize)));
    } else {
      NonConstSpaceBeg =
          reinterpret_cast<uptr>(MmapNoAccess(TotalSpaceSize));
      CHECK_NE(NonConstSpaceBeg, ~(uptr)0);
    }
    static u64 fmap[sizeof(Vmap)/sizeof(u64)+1];
    Forkmap = new(fmap) Vmap();
    static u64 pmap[sizeof(Pmap)/sizeof(u64)+1];
    Pagemap = new(pmap) Pmap();
    MapWithCallback(SpaceEnd(), AdditionalSize());
  }

  void MapWithCallback(uptr beg, uptr size) {
    CHECK_EQ(beg, reinterpret_cast<uptr>(MmapFixedOrDie(beg, size)));
    MapUnmapCallback().OnMap(beg, size);
  }

  void OnMap(uptr beg, uptr size) {
    MapUnmapCallback().OnMap(beg, size);
  }

  void UnmapWithCallback(uptr beg, uptr size) {
    MapUnmapCallback().OnUnmap(beg, size);
    UnmapOrDie(reinterpret_cast<void *>(beg), size);
  }

  void OnUnmap(uptr beg, uptr size) {
    MapUnmapCallback().OnUnmap(beg, size);
  }

  static bool CanAllocate(uptr size, uptr alignment) {
    return size <= SizeClassMap::kMaxSize &&
      alignment <= SizeClassMap::kMaxSize;
  }

  NOINLINE void ReturnToAllocator(AllocatorStats *stat, uptr class_id,
                                  uptr *begin, uptr size) {
    return;
  }

  NOINLINE void GetFromAllocator(AllocatorStats *stat, uptr class_id,
                                 uptr *begin, uptr size) {
    RegionInfo *region = GetRegionInfo(class_id);
    BlockingMutexLock l(&region->mutex);
    if (region->pos == 0) region->pos = GetRegionBeginBySizeClass(class_id);
    if (region->pos >= GetRegionBeginBySizeClass(class_id + 1)) {
      Printf("Address space insufficient\n");
      Die();
    }
    *begin = region->pos;
    region->pos += size;
  }


  bool PointerIsMine(const void *p) {
    uptr P = reinterpret_cast<uptr>(p);
    if (kUsingConstantSpaceBeg && (kSpaceBeg % kSpaceSize) == 0)
      return P / kSpaceSize == kSpaceBeg / kSpaceSize;
    return P >= SpaceBeg() && P < SpaceEnd();
  }

  uptr GetRegionBegin(const void *p) {
    if (kUsingConstantSpaceBeg)
      return reinterpret_cast<uptr>(p) & ~(kRegionSize - 1);
    uptr space_beg = SpaceBeg();
    return ((reinterpret_cast<uptr>(p)  - space_beg) & ~(kRegionSize - 1)) +
        space_beg;
  }

  uptr GetRegionBeginBySizeClass(uptr class_id) {
    return SpaceBeg() + kRegionSize * class_id;
  }

  uptr GetSizeClass(const void *p) {
    if (kUsingConstantSpaceBeg && (kSpaceBeg % kSpaceSize) == 0)
      return ((reinterpret_cast<uptr>(p)) / kRegionSize) % kNumClassesRounded;
    return ((reinterpret_cast<uptr>(p) - SpaceBeg()) / kRegionSize) %
           kNumClassesRounded;
  }

  uptr GetActuallyAllocatedSize(void *p) {
    CHECK(PointerIsMine(p));
    return ClassIdToSize(GetSizeClass(p));
  }

  uptr ClassID(uptr size) { return SizeClassMap::ClassID(size); }

  uptr TotalMemoryUsed() {
    uptr res = 0;
    for (uptr i = 0; i < kNumClasses; i++)
      res += GetRegionInfo(i)->allocated_user;
    return res;
  }

  // Test-only.
  void TestOnlyUnmap() {
    UnmapWithCallback(SpaceBeg(), kSpaceSize + AdditionalSize());
  }


  // ForceLock() and ForceUnlock() are needed to implement Darwin malloc zone
  // introspection API.
  void ForceLock() {
    for (uptr i = 0; i < kNumClasses; i++) {
      GetRegionInfo(i)->mutex.Lock();
    }
  }

  void ForceUnlock() {
    for (int i = (int)kNumClasses - 1; i >= 0; i--) {
      GetRegionInfo(i)->mutex.Unlock();
    }
  }

  // Iterate over all existing chunks.
  // The allocator must be locked when calling this function.

  static uptr ClassIdToSize(uptr class_id) {
    return SizeClassMap::Size(class_id);
  }

  static uptr AdditionalSize() {
    return RoundUpTo(sizeof(RegionInfo) * kNumClassesRounded,
                     GetPageSizeCached());
  }

  typedef SizeClassMap SizeClassMapT;
  static const uptr kNumClasses = SizeClassMap::kNumClasses;
  static const uptr kNumClassesRounded = SizeClassMap::kNumClassesRounded;

 private:
  static const uptr kRegionSize = kSpaceSize / kNumClassesRounded;
  // FreeArray is the array of free-d chunks (stored as 4-byte offsets).
  // In the worst case it may reguire kRegionSize/SizeClassMap::kMinSize
  // elements, but in reality this will not happen. For simplicity we
  // dedicate 1/8 of the region's virtual space to FreeArray.

  static const bool kUsingConstantSpaceBeg = kSpaceBeg != ~(uptr)0;
  uptr NonConstSpaceBeg;
  uptr SpaceBeg() const {
    return kUsingConstantSpaceBeg ? kSpaceBeg : NonConstSpaceBeg;
  }
  uptr SpaceEnd() const { return  SpaceBeg() + kSpaceSize; }
  // kRegionSize must be >= 2^32.
  // COMPILER_CHECK((kRegionSize) >= (1ULL << (SANITIZER_WORDSIZE / 2)));
  // kRegionSize must be <= 2^36, see CompactPtrT.
  // COMPILER_CHECK((kRegionSize) <= (1ULL << (SANITIZER_WORDSIZE / 2 + 4)));



  struct RegionInfo {
    BlockingMutex mutex;
    uptr allocated_user;  // Bytes allocated for user memory.
    uptr allocated_meta;  // Bytes allocated for metadata.
    uptr mapped_user;  // Bytes mapped for user memory.
    uptr mapped_meta;  // Bytes mapped for metadata.
    uptr n_allocated, n_freed;  // Just stats.
    uptr pos;
  };
  COMPILER_CHECK(sizeof(RegionInfo) >= kCacheLineSize);

  RegionInfo *GetRegionInfo(uptr class_id) {
    CHECK_LT(class_id, kNumClasses);
    RegionInfo *regions =
        reinterpret_cast<RegionInfo *>(SpaceBeg() + kSpaceSize);
    return &regions[class_id];
  }

  uptr GetMetadataEnd(uptr region_beg) {
    return region_beg + kRegionSize;
  }
};


template <class AliasingAllocator>
struct AliasingAllocatorLocalCache {
  typedef AliasingAllocator Allocator;
  static const uptr kNumClasses = AliasingAllocator::kNumClasses;
  typedef typename Allocator::SizeClassMapT SizeClassMap;

  void Init(AllocatorGlobalStats *s) {
    page_size_ = GetPageSizeCached();
    stats_.Init();
    if (s)
      s->Register(&stats_);
  }

  void Destroy(AliasingAllocator *allocator, AllocatorGlobalStats *s) {
    if (s)
      s->Unregister(&stats_);
  }

  uptr MmapAliasing(uptr vpage, uptr size) {
    uptr res = internal_mmap(reinterpret_cast<void*>(vpage), size,
                             PROT_READ|PROT_WRITE,
                             MAP_FIXED|MAP_ANON|MAP_SHARED,
                             -1, 0);
    return res;
  }

  uptr MremapAliasing(uptr orig, uptr vpage, uptr size) {
    uptr res =  internal_mremap(reinterpret_cast<void*>(orig), 0, size,
                                MREMAP_FIXED|MREMAP_MAYMOVE,
                                reinterpret_cast<void*>(vpage));
    return res;
  }

  void AllocateChunk(AliasingAllocator *allocator, uptr class_id,
                     uptr vaddr, uptr size) {
    PerClass *c = &per_class_[class_id];
    uptr vbase = RoundDownTo(vaddr, page_size_);
    uptr offset = vaddr - vbase;
    uptr csize = SizeClassMap::Size(class_id);
    Freed *chunk = c->free_list.front();
    while (chunk != nullptr) {
      if (chunk->offset == offset && size == chunk->size) {
        c->free_list.remove(chunk);
        break;
      }
      chunk = chunk->next;
    }
    if (chunk) {
      BlockingMutexLock l(&chunk->page->mutex);
      CHECK_NE(chunk->page, 0);
      if (chunk->page->vlist.empty()) {
        uptr res = MmapAliasing(vbase, page_size_);
        CHECK_NE(res, 0);
        Vpage *v = reinterpret_cast<Vpage *>(InternalAlloc(sizeof(Vpage)));
        CHECK_NE(v, 0);
        v->addr = res;
        chunk->page->vlist.clear();
        chunk->page->vlist.push_back(v);
        {
          Vmap::Handle h(allocator->Forkmap, res);
          *(h.operator->()) = v;
        }
        {
          Pmap::Handle h(allocator->Pagemap, res);
          *(h.operator->()) = chunk->page;
        }
      } else {
        uptr orig_vaddr = chunk->page->vlist.front()->addr;
        uptr res = MremapAliasing(orig_vaddr, vbase, page_size_);
        CHECK_NE(res, 0);
        Vpage *v = reinterpret_cast<Vpage *>(InternalAlloc(sizeof(Vpage)));
        CHECK_NE(v, 0);
        v->addr = res;
        chunk->page->vlist.push_back(v);
        {
          Vmap::Handle h(allocator->Forkmap, res);
          *(h.operator->()) = v;
        }
        {
          Pmap::Handle h(allocator->Pagemap, res);
          *(h.operator->()) = chunk->page;
        }
      }
      InternalFree(chunk);
    } else {
      uptr res = MmapAliasing(vbase, page_size_);
      CHECK_NE(res, 0);
      Ppage *p = reinterpret_cast<Ppage *>(InternalAlloc(sizeof(Ppage)));
      internal_memset(p, 0, sizeof(Ppage));
      CHECK_NE(p, 0);
      BlockingMutexLock l(&p->mutex);
      Vpage *v = reinterpret_cast<Vpage *>(InternalAlloc(sizeof(Vpage)));
      CHECK_NE(v, 0);
      v->addr = res;
      {
        Vmap::Handle h(allocator->Forkmap, res);
        *(h.operator->()) = v;
      }
      {
        Pmap::Handle h(allocator->Pagemap, res);
        *(h.operator->()) = p;
      }

      // back populate
      uptr pos = offset + csize;
      while (pos < page_size_) {
        Freed *f = reinterpret_cast<Freed *>(InternalAlloc(sizeof(Freed)));
        if (page_size_ - pos < csize) {
          f->size = page_size_ - pos;
        } else {
          f->size = csize;
        }
        f->offset = pos;
        f->page = p;
        c->free_list.push_back(f);
        pos += csize;
      }

      // front populate
      pos = 0;
      while (pos < offset) {
        Freed *f = reinterpret_cast<Freed *>(InternalAlloc(sizeof(Freed)));
        if (offset - pos < csize) {
          f->size = offset - pos;
        } else {
          f->size = csize;
        }
        f->offset = pos;
        f->page = p;
        c->free_list.push_back(f);
        pos += csize;
      }
    }
  }

  void *Allocate(AliasingAllocator *allocator, uptr class_id, uptr rz_size) {
    struct PerClass *c = &per_class_[class_id];
    uptr size = Allocator::ClassIdToSize(class_id);
    if (size + c->lower_bound > c->upper_bound) {
      allocator->GetFromAllocator(&stats_, class_id, &c->lower_bound,
                                  kPopulateSize);
      CHECK_NE(c->lower_bound, 0ULL);
      c->upper_bound = c->lower_bound + kPopulateSize;
    }
    void *res = reinterpret_cast<void *>(c->lower_bound);
    uptr vaddr = c->lower_bound;
    uptr vpage = RoundDownTo(vaddr, page_size_);
    uptr offset = vaddr - vpage;
    uptr remain = size;

    if (offset != 0) {
      uptr alloc_size = (page_size_ - offset) < remain ?
        (page_size_ - offset) : remain;
      AllocateChunk(allocator, class_id, vaddr, alloc_size);
      allocator->OnMap(RoundDownTo(vaddr, page_size_), page_size_);
      remain -= alloc_size;
      vaddr += alloc_size;
    }
    if (remain >= page_size_) {
      CHECK(IsAligned(vaddr, page_size_));
      uptr alloc_size = RoundDownTo(remain, page_size_);
      allocator->OnMap(vaddr, alloc_size);
      MmapFixedOrDie(vaddr, alloc_size);
      remain -= alloc_size;
      vaddr += alloc_size;
    }
    if (remain > 0) {
      CHECK(IsAligned(vaddr, page_size_));
      uptr alloc_size = remain;
      allocator->OnMap(vaddr, page_size_);
      AllocateChunk(allocator, class_id, vaddr, alloc_size);
      remain -= alloc_size;
      vaddr += alloc_size;
    }

    c->lower_bound += (size + rz_size);
    return res;
  }

  void DeallocateChunk(AliasingAllocator *allocator, uptr class_id,
                       uptr vaddr, uptr size) {
    PerClass *c = &per_class_[class_id];
    uptr vbase = RoundDownTo(vaddr, page_size_);
    uptr offset = vaddr - vbase;
    Vpage *vpage = nullptr;
    Ppage *ppage = nullptr;
    {
      Vmap::Handle h(allocator->Forkmap, vbase, true);
      if (h.exists()) {
        vpage = *(h.operator->());
      }
    }
    {
      Pmap::Handle h(allocator->Pagemap, vbase, true);
      if (h.exists()) {
        ppage = *(h.operator->());
      }
    }
    CHECK_NE(vpage, 0);
    CHECK_NE(ppage, 0);
    BlockingMutexLock(&ppage->mutex);
    ppage->vlist.remove(vpage);
    Freed *f = reinterpret_cast<Freed *>(InternalAlloc(sizeof(Freed)));
    f->page = ppage;
    f->offset = offset;
    f->size = size;
    c->free_list.push_back(f);
  }

  void Deallocate(AliasingAllocator *allocator, uptr class_id, void *p) {
    uptr vaddr = reinterpret_cast<uptr>(p);
    uptr vpage = RoundDownTo(vaddr, page_size_);
    uptr offset = vaddr - vpage;
    uptr csize = Allocator::ClassIdToSize(class_id);
    uptr size = RoundUpTo(vaddr + csize - vpage, page_size_);

    uptr remain = csize;
    if (offset != 0) {
      uptr dealloc_size = (page_size_ - offset) < remain?
        (page_size_ - offset) : remain;
      DeallocateChunk(allocator, class_id, vaddr, dealloc_size);
      remain -= dealloc_size;
      vaddr += dealloc_size;
    }
    if (remain >= page_size_) {
      uptr dealloc_size = RoundDownTo(remain, page_size_);
      vaddr += dealloc_size;
      remain -= dealloc_size;
    }
    if (remain > 0) {
      uptr dealloc_size = remain;
      DeallocateChunk(allocator, class_id, vaddr, dealloc_size);
      remain -= dealloc_size;
      vaddr += dealloc_size;
    }
    MmapFixedNoAccess(vpage, size);
    return;
  }

  void Drain(AliasingAllocator *allocator) {
    // TODO(wookhyun): return to allocator
    return;
  }
  // private:
  struct PerClass {
    uptr lower_bound;
    uptr upper_bound;
    DoubleList<Freed> free_list;
  };
  PerClass per_class_[kNumClasses];
  AllocatorStats stats_;

  static const uptr kPopulateSize = 1ULL << 32;
  uptr page_size_;
};

template <class MapUnmapCallback = NoOpMapUnmapCallback>
class LargeMmapAliasing {
 public:
  void InitLinkerInitialized(bool may_return_null) {
    page_size_ = GetPageSizeCached();
    atomic_store(&may_return_null_, may_return_null, memory_order_relaxed);
  }

  void Init(bool may_return_null) {
    internal_memset(this, 0, sizeof(*this));
    InitLinkerInitialized(may_return_null);
  }

  void *Allocate(AllocatorStats *stat, uptr size, uptr alignment) {
    CHECK(IsPowerOfTwo(alignment));
    uptr map_size = RoundUpMapSize(size);
    if (alignment > page_size_)
      map_size += alignment;
    // Overflow.
    if (map_size < size) return ReturnNullOrDieOnBadRequest();
    uptr map_beg = reinterpret_cast<uptr>(
        MmapOrDie(map_size, "LargeMmapAllocator"));
    CHECK(IsAligned(map_beg, page_size_));
    MapUnmapCallback().OnMap(map_beg, map_size);
    uptr map_end = map_beg + map_size;
    uptr res = map_beg + page_size_;
    if (res & (alignment - 1))  // Align.
      res += alignment - (res & (alignment - 1));
    CHECK(IsAligned(res, alignment));
    CHECK(IsAligned(res, page_size_));
    CHECK_GE(res + size, map_beg);
    CHECK_LE(res + size, map_end);
    Header *h = GetHeader(res);
    h->size = size;
    h->map_beg = map_beg;
    h->map_size = map_size;
    uptr size_log = MostSignificantSetBitIndex(map_size);
    CHECK_LT(size_log, ARRAY_SIZE(stats.by_size_log));
    {
      SpinMutexLock l(&mutex_);
      uptr idx = n_chunks_++;
      chunks_sorted_ = false;
      CHECK_LT(idx, kMaxNumChunks);
      h->chunk_idx = idx;
      chunks_[idx] = h;
      stats.n_allocs++;
      stats.currently_allocated += map_size;
      stats.max_allocated = Max(stats.max_allocated, stats.currently_allocated);
      stats.by_size_log[size_log]++;
      stat->Add(AllocatorStatAllocated, map_size);
      stat->Add(AllocatorStatMapped, map_size);
    }
    return reinterpret_cast<void*>(res);
  }

  bool MayReturnNull() const {
    return atomic_load(&may_return_null_, memory_order_acquire);
  }

  void *ReturnNullOrDieOnBadRequest() {
    if (MayReturnNull()) return nullptr;
    ReportAllocatorCannotReturnNull(false);
  }

  void *ReturnNullOrDieOnOOM() {
    if (MayReturnNull()) return nullptr;
    ReportAllocatorCannotReturnNull(true);
  }

  void SetMayReturnNull(bool may_return_null) {
    atomic_store(&may_return_null_, may_return_null, memory_order_release);
  }

  void Deallocate(AllocatorStats *stat, void *p) {
    Header *h = GetHeader(p);
    {
      SpinMutexLock l(&mutex_);
      uptr idx = h->chunk_idx;
      CHECK_EQ(chunks_[idx], h);
      CHECK_LT(idx, n_chunks_);
      chunks_[idx] = chunks_[n_chunks_ - 1];
      chunks_[idx]->chunk_idx = idx;
      n_chunks_--;
      chunks_sorted_ = false;
      stats.n_frees++;
      stats.currently_allocated -= h->map_size;
      stat->Sub(AllocatorStatAllocated, h->map_size);
      stat->Sub(AllocatorStatMapped, h->map_size);
      uptr beg = h->map_beg;
      uptr size = h->map_size;
      MapUnmapCallback().OnUnmap(beg, size);
      UnmapOrDie(reinterpret_cast<void*>(beg), size);
      MmapFixedNoAccess(beg, size);
    }
  }

  uptr TotalMemoryUsed() {
    SpinMutexLock l(&mutex_);
    uptr res = 0;
    for (uptr i = 0; i < n_chunks_; i++) {
      Header *h = chunks_[i];
      CHECK_EQ(h->chunk_idx, i);
      res += RoundUpMapSize(h->size);
    }
    return res;
  }

  bool PointerIsMine(const void *p) {
    return GetBlockBegin(p) != nullptr;
  }

  uptr GetActuallyAllocatedSize(void *p) {
    return RoundUpTo(GetHeader(p)->size, page_size_);
  }

  // At least page_size_/2 metadata bytes is available.
  void *GetMetaData(const void *p) {
    // Too slow: CHECK_EQ(p, GetBlockBegin(p));
    if (!IsAligned(reinterpret_cast<uptr>(p), page_size_)) {
      Printf("%s: bad pointer %p\n", SanitizerToolName, p);
      CHECK(IsAligned(reinterpret_cast<uptr>(p), page_size_));
    }
    return GetHeader(p) + 1;
  }

  void *GetBlockBegin(const void *ptr) {
    uptr p = reinterpret_cast<uptr>(ptr);
    SpinMutexLock l(&mutex_);
    uptr nearest_chunk = 0;
    // Cache-friendly linear search.
    for (uptr i = 0; i < n_chunks_; i++) {
      uptr ch = reinterpret_cast<uptr>(chunks_[i]);
      if (p < ch) continue;  // p is at left to this chunk, skip it.
      if (p - ch < p - nearest_chunk)
        nearest_chunk = ch;
    }
    if (!nearest_chunk)
      return nullptr;
    Header *h = reinterpret_cast<Header *>(nearest_chunk);
    CHECK_GE(nearest_chunk, h->map_beg);
    CHECK_LT(nearest_chunk, h->map_beg + h->map_size);
    CHECK_LE(nearest_chunk, p);
    if (h->map_beg + h->map_size <= p)
      return nullptr;
    return GetUser(h);
  }

  void EnsureSortedChunks() {
    if (chunks_sorted_) return;
    SortArray(reinterpret_cast<uptr*>(chunks_), n_chunks_);
    for (uptr i = 0; i < n_chunks_; i++)
      chunks_[i]->chunk_idx = i;
    chunks_sorted_ = true;
  }

  // This function does the same as GetBlockBegin, but is much faster.
  // Must be called with the allocator locked.
  void *GetBlockBeginFastLocked(void *ptr) {
    mutex_.CheckLocked();
    uptr p = reinterpret_cast<uptr>(ptr);
    uptr n = n_chunks_;
    if (!n) return nullptr;
    EnsureSortedChunks();
    auto min_mmap_ = reinterpret_cast<uptr>(chunks_[0]);
    auto max_mmap_ =
        reinterpret_cast<uptr>(chunks_[n - 1]) + chunks_[n - 1]->map_size;
    if (p < min_mmap_ || p >= max_mmap_)
      return nullptr;
    uptr beg = 0, end = n - 1;
    // This loop is a log(n) lower_bound. It does not check for the exact match
    // to avoid expensive cache-thrashing loads.
    while (end - beg >= 2) {
      uptr mid = (beg + end) / 2;  // Invariant: mid >= beg + 1
      if (p < reinterpret_cast<uptr>(chunks_[mid]))
        end = mid - 1;  // We are not interested in chunks_[mid].
      else
        beg = mid;  // chunks_[mid] may still be what we want.
    }

    if (beg < end) {
      CHECK_EQ(beg + 1, end);
      // There are 2 chunks left, choose one.
      if (p >= reinterpret_cast<uptr>(chunks_[end]))
        beg = end;
    }

    Header *h = chunks_[beg];
    if (h->map_beg + h->map_size <= p || p < h->map_beg)
      return nullptr;
    return GetUser(h);
  }

  void PrintStats() {
    Printf("Stats: LargeMmapAllocator: allocated %zd times, "
           "remains %zd (%zd K) max %zd M; by size logs: ",
           stats.n_allocs, stats.n_allocs - stats.n_frees,
           stats.currently_allocated >> 10, stats.max_allocated >> 20);
    for (uptr i = 0; i < ARRAY_SIZE(stats.by_size_log); i++) {
      uptr c = stats.by_size_log[i];
      if (!c) continue;
      Printf("%zd:%zd; ", i, c);
    }
    Printf("\n");
  }

  // ForceLock() and ForceUnlock() are needed to implement Darwin malloc zone
  // introspection API.
  void ForceLock() {
    mutex_.Lock();
  }

  void ForceUnlock() {
    mutex_.Unlock();
  }

  // Iterate over all existing chunks.
  // The allocator must be locked when calling this function.
  void ForEachChunk(ForEachChunkCallback callback, void *arg) {
    EnsureSortedChunks();  // Avoid doing the sort while iterating.
    for (uptr i = 0; i < n_chunks_; i++) {
      auto t = chunks_[i];
      callback(reinterpret_cast<uptr>(GetUser(chunks_[i])), arg);
      // Consistency check: verify that the array did not change.
      CHECK_EQ(chunks_[i], t);
      CHECK_EQ(chunks_[i]->chunk_idx, i);
    }
  }

 private:
  static const int kMaxNumChunks = 1 << FIRST_32_SECOND_64(15, 18);
  struct Header {
    uptr map_beg;
    uptr map_size;
    uptr size;
    uptr chunk_idx;
  };

  Header *GetHeader(uptr p) {
    CHECK(IsAligned(p, page_size_));
    return reinterpret_cast<Header*>(p - page_size_);
  }
  Header *GetHeader(const void *p) {
    return GetHeader(reinterpret_cast<uptr>(p));
  }

  void *GetUser(Header *h) {
    CHECK(IsAligned((uptr)h, page_size_));
    return reinterpret_cast<void*>(reinterpret_cast<uptr>(h) + page_size_);
  }

  uptr RoundUpMapSize(uptr size) {
    return RoundUpTo(size, page_size_) + page_size_;
  }

  uptr page_size_;
  Header *chunks_[kMaxNumChunks];
  uptr n_chunks_;
  bool chunks_sorted_;
  struct Stats {
    uptr n_allocs, n_frees, currently_allocated, max_allocated, by_size_log[64];
  } stats;
  atomic_uint8_t may_return_null_;
  SpinMutex mutex_;
};

template <class PrimaryAllocator, class AllocatorCache,
          class SecondaryAllocator>  // NOLINT
class CombinedAliasing {
 public:
  void InitCommon(bool may_return_null, s32 release_to_os_interval_ms) {
    primary_.Init(release_to_os_interval_ms);
    atomic_store(&may_return_null_, may_return_null, memory_order_relaxed);
  }

  void InitLinkerInitialized(
      bool may_return_null, s32 release_to_os_interval_ms) {
    secondary_.InitLinkerInitialized(may_return_null);
    stats_.InitLinkerInitialized();
    InitCommon(may_return_null, release_to_os_interval_ms);
  }

  void Init(bool may_return_null, s32 release_to_os_interval_ms) {
    secondary_.Init(may_return_null);
    stats_.Init();
    InitCommon(may_return_null, release_to_os_interval_ms);
  }

  void *Allocate(AllocatorCache *cache, uptr size, uptr alignment,
                 uptr rz_size,
                 bool cleared = false, bool check_rss_limit = false) {
    // Returning 0 on malloc(0) may break a lot of code.
    if (size == 0)
      size = 1;
    if (size + alignment < size) return ReturnNullOrDieOnBadRequest();
    uptr original_size = size;
    // If alignment requirements are to be fulfilled by the frontend allocator
    // rather than by the primary or secondary, passing an alignment lower than
    // or equal to 8 will prevent any further rounding up, as well as the later
    // alignment check.
    if (alignment > 8)
      size = RoundUpTo(size, alignment);
    void *res;
    bool from_primary = primary_.CanAllocate(size, alignment);
    // The primary allocator should return a 2^x aligned allocation when
    // requested 2^x bytes, hence using the rounded up 'size' when being
    // serviced by the primary (this is no longer true when the primary is
    // using a non-fixed base address). The secondary takes care of the
    // alignment without such requirement, and allocating 'size' would use
    // extraneous memory, so we employ 'original_size'.
    if (from_primary)
      res = cache->Allocate(&primary_, primary_.ClassID(size), rz_size);
    else
      res = secondary_.Allocate(&stats_, original_size, alignment);
    if (alignment > 8)
      CHECK_EQ(reinterpret_cast<uptr>(res) & (alignment - 1), 0);
    // When serviced by the secondary, the chunk comes from a mmap allocation
    // and will be zero'd out anyway. We only need to clear our the chunk if
    // it was serviced by the primary, hence using the rounded up 'size'.
    if (cleared && res && from_primary)
      internal_bzero_aligned16(res, RoundUpTo(size, 16));
    return res;
  }

  bool MayReturnNull() const {
    return atomic_load(&may_return_null_, memory_order_acquire);
  }

  void *ReturnNullOrDieOnBadRequest() {
    if (MayReturnNull())
      return nullptr;
    ReportAllocatorCannotReturnNull(false);
  }

  void *ReturnNullOrDieOnOOM() {
    if (MayReturnNull()) return nullptr;
    ReportAllocatorCannotReturnNull(true);
  }

  void SetMayReturnNull(bool may_return_null) {
    secondary_.SetMayReturnNull(may_return_null);
    atomic_store(&may_return_null_, may_return_null, memory_order_release);
  }


  void Deallocate(AllocatorCache *cache, void *p) {
    if (!p) return;
    if (primary_.PointerIsMine(p))
      cache->Deallocate(&primary_, primary_.GetSizeClass(p), p);
    else
      secondary_.Deallocate(&stats_, p);
  }

  void *Reallocate(AllocatorCache *cache, void *p, uptr new_size,
                   uptr alignment) {
    if (!p)
      return Allocate(cache, new_size, alignment);
    if (!new_size) {
      Deallocate(cache, p);
      return nullptr;
    }
    CHECK(PointerIsMine(p));
    uptr old_size = GetActuallyAllocatedSize(p);
    uptr memcpy_size = Min(new_size, old_size);
    void *new_p = Allocate(cache, new_size, alignment);
    if (new_p)
      internal_memcpy(new_p, p, memcpy_size);
    Deallocate(cache, p);
    return new_p;
  }

  bool PointerIsMine(void *p) {
    if (primary_.PointerIsMine(p))
      return true;
    return secondary_.PointerIsMine(p);
  }

  bool FromPrimary(void *p) {
    return primary_.PointerIsMine(p);
  }

  uptr GetActuallyAllocatedSize(void *p) {
    if (primary_.PointerIsMine(p))
      return primary_.GetActuallyAllocatedSize(p);
    return secondary_.GetActuallyAllocatedSize(p);
  }

  uptr TotalMemoryUsed() {
    return primary_.TotalMemoryUsed() + secondary_.TotalMemoryUsed();
  }

  void TestOnlyUnmap() { primary_.TestOnlyUnmap(); }


  void InitCache(AllocatorCache *cache) {
    cache->Init(&stats_);
  }

  void DestroyCache(AllocatorCache *cache) {
    cache->Destroy(&primary_, &stats_);
  }

  void SwallowCache(AllocatorCache *cache) {
    cache->Drain(&primary_);
  }

  void GetStats(AllocatorStatCounters s) const {
    stats_.Get(s);
  }

  void PrintStats() {
    primary_.PrintStats();
    secondary_.PrintStats();
  }

  // ForceLock() and ForceUnlock() are needed to implement Darwin malloc zone
  // introspection API.
  void ForceLock() {
    primary_.ForceLock();
    secondary_.ForceLock();
  }

  void ForceUnlock() {
    secondary_.ForceUnlock();
    primary_.ForceUnlock();
  }

  PrimaryAllocator GetPrimary() {
    return primary_;
  }

 private:
  PrimaryAllocator primary_;
  SecondaryAllocator secondary_;
  AllocatorGlobalStats stats_;
  atomic_uint8_t may_return_null_;
};

}  //  namespace __sanitizer

#endif  // SANITIZER_ALIASING_H
