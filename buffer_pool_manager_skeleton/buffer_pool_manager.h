#pragma once

#include <list>
#include <mutex>
#include <unordered_map>
#include <functional>
#include "page.h"
#include "async_disk_manager.h"
#include "replacer.h"

/**
 * BufferPoolManager reads disk pages to and from its internal buffer pool.
 */
class BufferPoolManager {
public:
    BufferPoolManager(size_t pool_size, AsyncDiskManager *disk_manager, Replacer *replacer);
    ~BufferPoolManager();

    // Fetch the requested page asynchronously from the buffer pool.
    void FetchPageAsync(page_id_t page_id, std::function<void(Page*)> callback);

    // Synchronous convenience wrapper for FetchPage
    Page *FetchPage(page_id_t page_id);

    // Unpin the target page from the buffer pool (synchronous, in-memory only).
    bool UnpinPage(page_id_t page_id, bool is_dirty);

    // Flush the target page asynchronously to disk.
    void FlushPageAsync(page_id_t page_id, std::function<void(bool)> callback);

    // Synchronous convenience wrapper for FlushPage
    bool FlushPage(page_id_t page_id);

    // Create a new page in the buffer pool asynchronously.
    void NewPageAsync(std::function<void(Page*)> callback);

    // Synchronous convenience wrapper for NewPage
    Page *NewPage(page_id_t *page_id);

    // Delete a page from the buffer pool asynchronously.
    void DeletePageAsync(page_id_t page_id, std::function<void(bool)> callback);

    // Synchronous convenience wrapper for DeletePage
    bool DeletePage(page_id_t page_id);

    // Flush all dirty pages asynchronously.
    void FlushAllPagesAsync(std::function<void(bool)> callback);

    // Poll completions from the underlying disk manager.
    inline int PollCompletions() { return disk_manager_->PollCompletions(); }

private:
    size_t pool_size_;             // Number of pages in the buffer pool
    Page *pages_;                  // Array of pages (the actual buffer pool memory)
    AsyncDiskManager *disk_manager_; // Pointer to the async disk manager
    Replacer *replacer_;           // Pointer to the page replacement policy
    
    std::unordered_map<page_id_t, frame_id_t> page_table_; // Maps page_id to frame_id
    std::list<frame_id_t> free_list_;                      // List of free frames
    std::mutex latch_;                                     // Latch for thread safety

    // Track active page fetches to coalesce multiple concurrent requests for the same page
    std::unordered_map<page_id_t, std::vector<std::function<void(Page*)>>> in_flight_fetches_;

    // Completion helpers called from async callbacks
    void CompleteFetch(page_id_t page_id, frame_id_t frame_id, bool success);
    void CompleteNewPage(page_id_t new_page_id, frame_id_t frame_id, bool success, std::function<void(Page*)> callback);
};
