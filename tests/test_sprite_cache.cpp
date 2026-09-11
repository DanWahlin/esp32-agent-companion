#include "../firmware/Copilot/src/SpriteBlockCache.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <vector>

static size_t allocations = 0;

void* operator new(size_t bytes) {
  ++allocations;
  if (void* memory = std::malloc(bytes ? bytes : 1)) return memory;
  throw std::bad_alloc();
}
void* operator new[](size_t bytes) { return ::operator new(bytes); }
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, size_t) noexcept { std::free(memory); }
void* operator new(size_t bytes, std::align_val_t alignment) {
  ++allocations;
  void* memory = nullptr;
  if (!posix_memalign(&memory, static_cast<size_t>(alignment), bytes ? bytes : 1)) return memory;
  throw std::bad_alloc();
}
void* operator new[](size_t bytes, std::align_val_t alignment) {
  return ::operator new(bytes, alignment);
}
void operator delete(void* memory, std::align_val_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::align_val_t) noexcept { std::free(memory); }
void operator delete(void* memory, size_t, std::align_val_t) noexcept { std::free(memory); }
void operator delete[](void* memory, size_t, std::align_val_t) noexcept { std::free(memory); }

using copilot::SpriteBlockCache;
constexpr size_t page = copilot::kSpriteCachePageBytes;
constexpr size_t window = copilot::kSpriteCacheWindowBytes;
constexpr size_t capacity = copilot::kSpriteCacheSlots * window;
constexpr size_t guard = 37;
constexpr uint8_t sentinel = 0xa5;

struct NoAllocations {
  size_t before = allocations;
  ~NoAllocations() { assert(allocations == before); }
};

struct Fixture {
  std::vector<uint8_t> storage;
  std::vector<uint8_t> flash;
  SpriteBlockCache cache;

  explicit Fixture(size_t fileSize = 20 * page + 193)
      : storage(capacity + 2 * guard, sentinel),
        flash(fileSize),
        cache(storage.data() + guard, capacity, fileSize) {
    for (size_t i = 0; i < fileSize; ++i)
      flash[i] = static_cast<uint8_t>((i * 37) ^ (i >> 8) ^ (i >> 16));
  }

  ~Fixture() {
    for (size_t i = 0; i < guard; ++i) {
      assert(storage[i] == sentinel);
      assert(storage[guard + capacity + i] == sentinel);
    }
  }

  void fill(int slot) {
    const auto job = cache.job(slot);
    assert(job.data && job.size && job.size <= window);
    assert(job.offset < flash.size() && job.size <= flash.size() - job.offset);
    assert(job.data == storage.data() + guard + static_cast<size_t>(slot) * window);
    std::memcpy(job.data, flash.data() + job.offset, job.size);
    assert(std::memcmp(job.data, flash.data() + job.offset, job.size) == 0);
  }

  int load(size_t offset, size_t size = 1) {
    const auto lookup = cache.acquire(offset, size);
    assert(!lookup.data && lookup.load && lookup.slot >= 0);
    fill(lookup.slot);
    cache.complete(lookup.slot, true);
    return lookup.slot;
  }

  SpriteBlockCache::Lookup hit(size_t offset, size_t size) {
    const auto lookup = cache.acquire(offset, size);
    assert(lookup.data && !lookup.load && lookup.slot >= 0);
    assert(std::memcmp(lookup.data, flash.data() + offset, size) == 0);
    return lookup;
  }
};

static void fallback(SpriteBlockCache::Lookup lookup) {
  assert(!lookup.data && lookup.slot == -1 && !lookup.load);
}

static void noJob(SpriteBlockCache::Job job) {
  assert(!job.data && !job.offset && !job.size);
}

static void boundariesAndOverlap() {
  Fixture f;
  NoAllocations noAllocations;
  assert(f.cache.valid());
  const int first = f.load(page - 17, page);
  auto a = f.hit(page - 17, page);
  auto b = f.hit(page + 111, 127);
  assert(a.slot == first && b.slot == first);
  assert(b.data - a.data == 128);
  const int second = f.load(2 * page - 9, page);
  const auto pending = f.hit(2 * page - 9, page);
  assert(second != first && pending.slot == second);
  // Both windows cover this request; reuse either rather than reserve a third.
  auto shared = f.hit(page + 42, 80);
  assert(shared.slot == first || shared.slot == second);
  noJob(f.cache.job(first));
  noJob(f.cache.job(second));
  f.cache.complete(first, false);  // Non-loading completion cannot revoke a pin.
  assert(std::memcmp(a.data, f.flash.data() + page - 17, page) == 0);
  f.cache.release(a.slot);
  f.cache.release(b.slot);
  f.cache.release(shared.slot);
  f.cache.release(pending.slot);
}

static void outstandingJobs() {
  Fixture f;
  NoAllocations noAllocations;
  SpriteBlockCache::Lookup loads[4];
  SpriteBlockCache::Job jobs[4];
  for (int i = 0; i < 4; ++i) {
    loads[i] = f.cache.acquire(static_cast<size_t>(i) * 3 * page, page);
    assert(loads[i].load && loads[i].slot == i);
    jobs[i] = f.cache.job(i);
    fallback(f.cache.acquire(static_cast<size_t>(i) * 3 * page + page - 1, 2));
  }
  fallback(f.cache.acquire(15 * page, 1));
  // A release token never changes a loading slot or permits its reuse.
  for (int i = 0; i < 4; ++i) f.cache.release(i);
  fallback(f.cache.acquire(15 * page, 1));
  for (int i = 3; i >= 0; --i) {
    assert(f.cache.job(i).data == jobs[i].data);
    f.fill(i);  // Worker writes without touching metadata.
    fallback(f.cache.acquire(jobs[i].offset, 1));  // Not yet published.
    f.cache.complete(i, true);
    const auto hit = f.hit(jobs[i].offset, page);
    assert(hit.slot == i);
    // Keep each completed slot pinned while the other jobs remain outstanding.
  }
  fallback(f.cache.acquire(15 * page, 1));
  for (int i = 0; i < 4; ++i) f.cache.release(i);
  assert(f.cache.acquire(15 * page, 1).load);
}

static void readyBeforeOverlappingLoad() {
  Fixture f;
  NoAllocations noAllocations;
  auto pending = f.cache.acquire(0, 1);
  assert(pending.load);
  // This block does not fit the pending window, so it needs its own window.
  const int ready = f.load(2 * page - 1, 2);
  auto shared = f.hit(page + 1, 12);
  assert(shared.slot == ready);
  f.cache.release(shared.slot);
  f.fill(pending.slot);
  f.cache.complete(pending.slot, true);
}

static void lruAndPins() {
  Fixture f;
  NoAllocations noAllocations;
  int slots[4];
  for (int i = 0; i < 4; ++i) slots[i] = f.load(static_cast<size_t>(i) * 3 * page);
  auto refreshed = f.hit(0, 10);
  f.cache.release(refreshed.slot);
  auto oldest = f.cache.acquire(12 * page, 1);
  assert(oldest.load && oldest.slot == slots[1]);
  f.fill(oldest.slot);
  f.cache.complete(oldest.slot, true);

  auto pin1 = f.hit(6 * page, 20);
  auto pin2 = f.hit(6 * page, 20);
  for (size_t offset : {9 * page, size_t(0), 12 * page}) {
    auto hit = f.hit(offset, 1);
    f.cache.release(hit.slot);
  }
  // The oldest slot is pinned twice and must be skipped, even after one release.
  f.cache.release(pin1.slot);
  auto replacement = f.cache.acquire(15 * page, 1);
  assert(replacement.load && replacement.slot == slots[3]);
  assert(std::memcmp(pin2.data, f.flash.data() + 6 * page, 20) == 0);
  f.fill(replacement.slot);
  f.cache.complete(replacement.slot, true);
  f.cache.release(pin2.slot);
  auto unpinned = f.cache.acquire(18 * page, 1);
  assert(unpinned.load && unpinned.slot == slots[2]);
}

static void failureAndBounds() {
  Fixture f(3 * page + 23);
  NoAllocations noAllocations;
  for (auto request : {f.cache.acquire(0, 0), f.cache.acquire(0, page + 1),
                       f.cache.acquire(f.flash.size(), 1),
                       f.cache.acquire(f.flash.size() - 1, 2),
                       f.cache.acquire(std::numeric_limits<size_t>::max(), 1),
                       f.cache.acquire(1, std::numeric_limits<size_t>::max())})
    fallback(request);
  for (int i = 0; i < 4; ++i) noJob(f.cache.job(i));
  auto failed = f.cache.acquire(0, 1);
  assert(failed.load);
  f.fill(failed.slot);
  f.cache.complete(failed.slot, false);
  noJob(f.cache.job(failed.slot));
  auto retry = f.cache.acquire(0, 1);
  assert(retry.load && retry.slot == failed.slot);
  // Queue rejection also frees the reservation without a worker write.
  f.cache.complete(retry.slot, false);
  assert(f.load(0) == failed.slot);
  auto hit = f.hit(0, page);
  f.cache.release(hit.slot);

  auto last = f.cache.acquire(f.flash.size() - 1, 1);
  assert(last.load);
  auto job = f.cache.job(last.slot);
  assert(job.offset == 3 * page && job.size == 23);
  f.fill(last.slot);
  assert(job.data[job.size] == sentinel);
  f.cache.complete(last.slot, true);
  auto tail = f.hit(3 * page, 23);
  f.cache.release(tail.slot);
  // A straddling block near EOF uses a truncated two-page window.
  auto straddle = f.cache.acquire(3 * page - 8, 31);
  assert(straddle.load);
  job = f.cache.job(straddle.slot);
  assert(job.offset == 2 * page && job.size == page + 23);
  f.fill(straddle.slot);
  assert(job.data[job.size] == sentinel);
  f.cache.complete(straddle.slot, true);
  tail = f.hit(3 * page - 8, 31);
  f.cache.release(tail.slot);
  for (int invalid : {-1, 4, std::numeric_limits<int>::max()}) {
    noJob(f.cache.job(invalid));
    f.cache.complete(invalid, true);
    f.cache.release(invalid);
  }
  f.cache.release(tail.slot);
}

static void disableAndUnavailable() {
  Fixture f;
  NoAllocations noAllocations;
  f.load(0);
  auto pin = f.hit(0, page);
  auto load = f.cache.acquire(3 * page, 1);
  assert(load.load);
  const auto job = f.cache.job(load.slot);
  f.cache.disable();
  f.cache.disable();
  assert(!f.cache.valid());
  fallback(f.cache.acquire(0, page));
  fallback(f.cache.acquire(9 * page, 1));
  noJob(f.cache.job(load.slot));
  // An already-issued job may finish writing after disable, but never publish.
  std::memcpy(job.data, f.flash.data() + job.offset, job.size);
  f.cache.complete(load.slot, true);
  fallback(f.cache.acquire(3 * page, 1));
  assert(std::memcmp(pin.data, f.flash.data(), page) == 0);
  f.cache.complete(pin.slot, false);
  assert(std::memcmp(pin.data, f.flash.data(), page) == 0);
  f.cache.release(pin.slot);
  f.cache.release(pin.slot);
  assert(std::memcmp(pin.data, f.flash.data(), page) == 0);
  for (int i = 0; i < 4; ++i) noJob(f.cache.job(i));

  SpriteBlockCache unavailable(nullptr, capacity, page);
  SpriteBlockCache undersized(f.storage.data() + guard, capacity - 1, page);
  SpriteBlockCache empty(f.storage.data() + guard, capacity, 0);
  for (SpriteBlockCache* cache : {&unavailable, &undersized, &empty}) {
    assert(!cache->valid());
    fallback(cache->acquire(0, 1));
    noJob(cache->job(0));
    cache->complete(0, true);
    cache->release(0);
    cache->disable();
  }
}

static void maximumFileSize() {
  std::vector<uint8_t> storage(capacity + 2 * guard, sentinel);
  NoAllocations noAllocations;
  const size_t max = std::numeric_limits<size_t>::max();
  SpriteBlockCache cache(storage.data() + guard, capacity, max);
  auto lookup = cache.acquire(max - page, page);
  assert(lookup.load);
  auto job = cache.job(lookup.slot);
  assert(job.offset == max - (2 * page - 1));
  assert(job.size == 2 * page - 1);
  std::memset(job.data, 0x76, job.size);
  cache.complete(lookup.slot, true);
  lookup = cache.acquire(max - page, page);
  assert(lookup.data && lookup.data[page - 1] == 0x76);
  cache.release(lookup.slot);
  fallback(cache.acquire(max - 1, 2));
  for (size_t i = 0; i < guard; ++i) {
    assert(storage[i] == sentinel);
    assert(storage[guard + capacity + i] == sentinel);
  }
}

static void randomizedInterleaving() {
  Fixture f;
  NoAllocations noAllocations;
  struct Held {
    SpriteBlockCache::Lookup lookup{nullptr, -1, false};
    size_t offset = 0;
    size_t size = 0;
  };
  Held held[12];
  SpriteBlockCache::Job pending[4]{};
  uint32_t seed = 0x329841;
  auto random = [&seed]() {
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return seed;
  };
  for (size_t iteration = 0; iteration < 5000; ++iteration) {
    const size_t selected = random() % 4;
    if (pending[selected].data && random() % 3 == 0) {
      const auto job = pending[selected];
      // Finish the delayed second half before completion, including failures.
      const size_t half = job.size / 2;
      std::memcpy(job.data + half, f.flash.data() + job.offset + half, job.size - half);
      const bool verified = random() % 5 != 0;
      if (verified) assert(std::memcmp(job.data, f.flash.data() + job.offset, job.size) == 0);
      f.cache.complete(static_cast<int>(selected), verified);
      pending[selected] = {};
    }
    Held& target = held[random() % 12];
    if (target.lookup.data) {
      f.cache.release(target.lookup.slot);
      target = {};
    }
    const size_t offset = random() % f.flash.size();
    const size_t size = std::min<size_t>(1 + random() % page, f.flash.size() - offset);
    auto lookup = f.cache.acquire(offset, size);
    if (lookup.data) {
      assert(!pending[lookup.slot].data);
      target = {lookup, offset, size};
    } else if (lookup.load) {
      assert(lookup.slot >= 0 && lookup.slot < 4 && !pending[lookup.slot].data);
      for (const Held& pin : held)
        assert(!pin.lookup.data || pin.lookup.slot != lookup.slot);
      auto job = f.cache.job(lookup.slot);
      pending[lookup.slot] = job;
      std::memcpy(job.data, f.flash.data() + job.offset, job.size / 2);
    } else {
      fallback(lookup);
    }
    for (const Held& pin : held) {
      if (pin.lookup.data)
        assert(std::memcmp(pin.lookup.data, f.flash.data() + pin.offset, pin.size) == 0);
    }
    for (int slot = 0; slot < 4; ++slot) {
      if (!pending[slot].data) continue;
      const auto job = f.cache.job(slot);
      assert(job.data == pending[slot].data && job.offset == pending[slot].offset
             && job.size == pending[slot].size);
    }
  }
  for (const Held& pin : held)
    if (pin.lookup.data) f.cache.release(pin.lookup.slot);
  for (int slot = 0; slot < 4; ++slot)
    if (pending[slot].data) f.cache.complete(slot, false);
}

int main() {
  static_assert(capacity == 524288, "Cache memory budget changed");
  static_assert(window == 2 * page, "Windows must span two pages");
  boundariesAndOverlap();
  outstandingJobs();
  readyBeforeOverlappingLoad();
  lruAndPins();
  failureAndBounds();
  disableAndUnavailable();
  maximumFileSize();
  randomizedInterleaving();
  std::puts("PASS: sprite cache bytes, overlap, LRU, pins, async jobs, retry, bounds, disable, no allocations and guards");
}
