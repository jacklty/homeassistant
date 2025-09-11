#ifndef TTL_CACHE_HH
#define TTL_CACHE_HH

#include <chrono>
#include <functional>
#include <iostream>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>

template <typename K, typename V>
class TTLCache {
 public:
  using Clock = std::chrono::steady_clock;
  using TimePoint = Clock::time_point;
  using TTLFunc = std::function<TimePoint(const K &, const V &)>;

  static TTLFunc FixedTTL(int ms) {
    return [ms](const K &, const V &) {
      return Clock::now() + std::chrono::milliseconds(ms);
    };
  };

 private:
  struct Entry {
    V value;
    TimePoint expireAt;
  };

  TTLFunc ttlFunc_;
  std::unordered_map<K, Entry> cache_;
  std::multimap<TimePoint, K> expiryOrder_;
  mutable std::mutex mutex_;

  void _evictExpiredUnlocked() {
    auto now = Clock::now();
    while (!expiryOrder_.empty()) {
      auto it = expiryOrder_.begin();
      if (now < it->first) {
        // earliest not yet expired
        break;
      }

      auto range = expiryOrder_.equal_range(it->first);
      // Although unlikely, muliple items might share the same expireAt,
      // so we immediately erase them all instead just the current entry.
      for (auto eit = range.first; eit != range.second;) {
        auto key = eit->second;
        cache_.erase(key);
        expiryOrder_.erase(eit);
      }
    }
  }

 public:
  explicit TTLCache(TTLFunc ttlFunc) : ttlFunc_(std::move(ttlFunc)) {}

  void put(const K &key, const V &value) {
    std::lock_guard<std::mutex> lock(mutex_);
    // Evict expired items eagerly
    _evictExpiredUnlocked();

    // Remove old expiry entry if the key already exists
    if (auto it = cache_.find(key); it != cache_.end()) {
      auto range = expiryOrder_.equal_range(it->second.expireAt);
      for (auto eit = range.first; eit != range.second; ++eit) {
        if (eit->second == key) {
          expiryOrder_.erase(eit);
          break;
        }
      }
    }

    // Insert new expiry entry and update/create cache entry
    auto expireAt = ttlFunc_(key, value);
    cache_[key] = {value, expireAt};
    expiryOrder_.emplace(expireAt, key);
  }

  bool get(const K &key, V &value) {
    std::lock_guard<std::mutex> lock(mutex_);
    // Evict expired items before perform a lookup
    _evictExpiredUnlocked();
    if (auto it = cache_.find(key); it != cache_.end()) {
      value = it->second.value;
      return true;
    }
    return false;
  }

  void evictExpired() {
    std::lock_guard<std::mutex> lock(mutex_);
    _evictExpiredUnlocked();
  }

  size_t size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return cache_.size();
  }

#ifdef UNIT_TEST
  friend class TTLCacheTest;
#endif
};

#endif  // TTL_CACHE_HPP
