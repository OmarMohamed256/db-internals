#pragma once

#include <cstring>
#include <shared_mutex>

using page_id_t = int32_t;
using frame_id_t = int32_t;
constexpr int PAGE_SIZE = 4096;
constexpr page_id_t INVALID_PAGE_ID = -1;

/**
 * Page represents a block of memory in the buffer pool.
 */
class Page {
public:
    Page() { ResetMemory(); }
    ~Page() = default;

    inline char *GetData() { return data_; }
    inline page_id_t GetPageId() const { return page_id_; }
    inline int GetPinCount() const { return pin_count_; }
    inline bool IsDirty() const { return is_dirty_; }

    // Latches for concurrent index operations (Latch crabbing / coupling)
    inline void WLatch() { rwlatch_.lock(); }
    inline void WUnlatch() { rwlatch_.unlock(); }
    inline void RLatch() { rwlatch_.lock_shared(); }
    inline void RUnlatch() { rwlatch_.unlock_shared(); }

protected:
    inline void ResetMemory() { memset(data_, 0, PAGE_SIZE); }

    alignas(PAGE_SIZE) char data_[PAGE_SIZE]{}; // The actual data of the page (4KB aligned for O_DIRECT)
    page_id_t page_id_ = INVALID_PAGE_ID;
    int pin_count_ = 0;
    bool is_dirty_ = false;
    std::shared_mutex rwlatch_;

    // BufferPoolManager needs access to modify page metadata
    friend class BufferPoolManager;
};
