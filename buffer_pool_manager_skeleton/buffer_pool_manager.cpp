#include "buffer_pool_manager.h"
#include <vector>
#include <memory>

BufferPoolManager::BufferPoolManager(size_t pool_size,
                                     AsyncDiskManager* disk_manager,
                                     Replacer* replacer)
    : pool_size_(pool_size), disk_manager_(disk_manager), replacer_(replacer) {
  // Allocate contiguous memory for the buffer pool pages
  pages_ = new Page[pool_size_];

  // Initially, all frames are free
  for (size_t i = 0; i < pool_size_; ++i) {
    free_list_.emplace_back(static_cast<frame_id_t>(i));
  }
}

BufferPoolManager::~BufferPoolManager() { delete[] pages_; }

void BufferPoolManager::FetchPageAsync(page_id_t page_id, std::function<void(Page*)> callback) {
    std::unique_lock<std::mutex> lock(latch_);

    // 1. Cache hit
    auto it = page_table_.find(page_id);
    if (it != page_table_.end()) {
        frame_id_t frame_id = it->second;
        Page* target_page = &pages_[frame_id];
        target_page->pin_count_++;
        replacer_->RecordAccess(frame_id);
        replacer_->Pin(frame_id);

        lock.unlock();
        callback(target_page);
        return;
    }

    // 2. Coalesce concurrent in-flight requests for the same page
    auto fit = in_flight_fetches_.find(page_id);
    if (fit != in_flight_fetches_.end()) {
        fit->second.push_back(callback);
        return;
    }

    // 3. Find a target frame_id (from free_list_ OR replacer)
    frame_id_t frame_id;
    if (!free_list_.empty()) {
        frame_id = free_list_.front();
        free_list_.pop_front();
    } else if (replacer_->Victim(&frame_id)) {
        Page* old_page = &pages_[frame_id];
        page_id_t old_page_id = old_page->GetPageId();

        if (old_page->IsDirty()) {
            // Asynchronously write dirty victim, then read new page
            page_table_.erase(old_page_id);
            in_flight_fetches_[page_id] = {callback};

            disk_manager_->WritePageAsync(old_page_id, old_page->GetData(), [this, frame_id, page_id](bool success) {
                if (!success) {
                    CompleteFetch(page_id, frame_id, false);
                    return;
                }
                disk_manager_->ReadPageAsync(page_id, pages_[frame_id].GetData(), [this, frame_id, page_id](bool success) {
                    CompleteFetch(page_id, frame_id, success);
                });
            });
            return;
        } else {
            page_table_.erase(old_page_id);
        }
    } else {
        lock.unlock();
        callback(nullptr); // No free frame and all frames are pinned
        return;
    }

    // 4. Initiate async read for the cache miss
    in_flight_fetches_[page_id] = {callback};
    disk_manager_->ReadPageAsync(page_id, pages_[frame_id].GetData(), [this, frame_id, page_id](bool success) {
        CompleteFetch(page_id, frame_id, success);
    });
}

void BufferPoolManager::CompleteFetch(page_id_t page_id, frame_id_t frame_id, bool success) {
    std::unique_lock<std::mutex> lock(latch_);

    Page* page = nullptr;
    if (success) {
        page = &pages_[frame_id];
        page->page_id_ = page_id;
        page->pin_count_ = 1;
        page->is_dirty_ = false;

        page_table_[page_id] = frame_id;
        replacer_->RecordAccess(frame_id);
        replacer_->Pin(frame_id);
    } else {
        // Recycle the frame if I/O failed
        free_list_.push_back(frame_id);
    }

    auto it = in_flight_fetches_.find(page_id);
    std::vector<std::function<void(Page*)>> callbacks;
    if (it != in_flight_fetches_.end()) {
        callbacks = std::move(it->second);
        in_flight_fetches_.erase(it);

        if (success && page) {
            // Adjust pin count based on the number of waiting requests
            page->pin_count_ = static_cast<int>(callbacks.size());
        }
    }

    lock.unlock();
    for (auto& cb : callbacks) {
        cb(page);
    }
}

void BufferPoolManager::NewPageAsync(std::function<void(Page*)> callback) {
    std::unique_lock<std::mutex> lock(latch_);

    // 1. Find a target frame_id (from free_list_ OR replacer)
    frame_id_t frame_id;
    if (!free_list_.empty()) {
        frame_id = free_list_.front();
        free_list_.pop_front();
    } else if (replacer_->Victim(&frame_id)) {
        Page* old_page = &pages_[frame_id];
        page_id_t old_page_id = old_page->GetPageId();

        if (old_page->IsDirty()) {
            page_table_.erase(old_page_id);
            page_id_t new_page_id = disk_manager_->AllocatePage();

            // Write dirty victim to disk before reusing the frame
            disk_manager_->WritePageAsync(old_page_id, old_page->GetData(), [this, frame_id, new_page_id, callback](bool success) {
                CompleteNewPage(new_page_id, frame_id, success, callback);
            });
            return;
        } else {
            page_table_.erase(old_page_id);
        }
    } else {
        lock.unlock();
        callback(nullptr); // All frames are pinned and no free frame is available
        return;
    }

    // 2. Allocate a new page ID and initialize immediately (no read required for new empty page)
    page_id_t new_page_id = disk_manager_->AllocatePage();
    Page* page = &pages_[frame_id];
    page->ResetMemory();
    page->page_id_ = new_page_id;
    page->pin_count_ = 1;
    page->is_dirty_ = false;

    page_table_[new_page_id] = frame_id;
    replacer_->RecordAccess(frame_id);
    replacer_->Pin(frame_id);

    lock.unlock();
    callback(page);
}

void BufferPoolManager::CompleteNewPage(page_id_t new_page_id, frame_id_t frame_id, bool success, std::function<void(Page*)> callback) {
    std::unique_lock<std::mutex> lock(latch_);

    Page* page = nullptr;
    if (success) {
        page = &pages_[frame_id];
        page->ResetMemory();
        page->page_id_ = new_page_id;
        page->pin_count_ = 1;
        page->is_dirty_ = false;

        page_table_[new_page_id] = frame_id;
        replacer_->RecordAccess(frame_id);
        replacer_->Pin(frame_id);
    } else {
        free_list_.push_back(frame_id);
    }

    lock.unlock();
    callback(page);
}

bool BufferPoolManager::UnpinPage(page_id_t page_id, bool is_dirty) {
    std::lock_guard<std::mutex> guard(latch_);

    // 1. Find the target frame in the page table
    auto it = page_table_.find(page_id);
    if (it == page_table_.end()) {
        return false;
    }

    frame_id_t frame_id = it->second;
    Page* page = &pages_[frame_id];

    // 2. If pin_count <= 0, cannot unpin
    if (page->GetPinCount() <= 0) {
        return false;
    }

    // 3. Update is_dirty flag
    if (is_dirty) {
        page->is_dirty_ = true;
    }

    // 4. Decrement pin_count
    page->pin_count_--;

    // 5. If pin_count reaches 0, notify the replacer that it can now be victimized
    if (page->GetPinCount() == 0) {
        replacer_->Unpin(frame_id);
    }

    return true;
}

void BufferPoolManager::FlushPageAsync(page_id_t page_id, std::function<void(bool)> callback) {
    std::unique_lock<std::mutex> lock(latch_);

    auto it = page_table_.find(page_id);
    if (it == page_table_.end() || page_id == INVALID_PAGE_ID) {
        lock.unlock();
        callback(false);
        return;
    }

    frame_id_t frame_id = it->second;
    Page* page = &pages_[frame_id];

    disk_manager_->WritePageAsync(page->GetPageId(), page->GetData(), [this, page_id, callback](bool success) {
        if (success) {
            std::lock_guard<std::mutex> guard(latch_);
            auto it2 = page_table_.find(page_id);
            if (it2 != page_table_.end()) {
                pages_[it2->second].is_dirty_ = false;
            }
        }
        callback(success);
    });
}

void BufferPoolManager::DeletePageAsync(page_id_t page_id, std::function<void(bool)> callback) {
    std::unique_lock<std::mutex> lock(latch_);

    auto it = page_table_.find(page_id);
    if (it == page_table_.end()) {
        lock.unlock();
        callback(true);
        return;
    }

    frame_id_t frame_id = it->second;
    Page* page = &pages_[frame_id];

    if (page->GetPinCount() > 0) {
        lock.unlock();
        callback(false); // Page is pinned, cannot delete
        return;
    }

    if (page->IsDirty()) {
        disk_manager_->WritePageAsync(page->GetPageId(), page->GetData(), [this, page_id, frame_id, callback](bool success) {
            if (success) {
                std::lock_guard<std::mutex> guard(latch_);
                disk_manager_->DeallocatePage(page_id);
                page_table_.erase(page_id);
                replacer_->Pin(frame_id);

                Page* p = &pages_[frame_id];
                p->ResetMemory();
                p->page_id_ = INVALID_PAGE_ID;
                p->pin_count_ = 0;
                p->is_dirty_ = false;
                free_list_.push_back(frame_id);
            }
            callback(success);
        });
    } else {
        disk_manager_->DeallocatePage(page_id);
        page_table_.erase(it);
        replacer_->Pin(frame_id);

        page->ResetMemory();
        page->page_id_ = INVALID_PAGE_ID;
        page->pin_count_ = 0;
        page->is_dirty_ = false;
        free_list_.push_back(frame_id);

        lock.unlock();
        callback(true);
    }
}

void BufferPoolManager::FlushAllPagesAsync(std::function<void(bool)> callback) {
    std::unique_lock<std::mutex> lock(latch_);

    std::vector<std::pair<page_id_t, frame_id_t>> dirty_pages;
    for (const auto& [page_id, frame_id] : page_table_) {
        if (page_id != INVALID_PAGE_ID && pages_[frame_id].IsDirty()) {
            dirty_pages.push_back({page_id, frame_id});
        }
    }

    if (dirty_pages.empty()) {
        lock.unlock();
        callback(true);
        return;
    }

    auto active_flushes = std::make_shared<size_t>(dirty_pages.size());
    auto overall_success = std::make_shared<bool>(true);

    for (const auto& [page_id, frame_id] : dirty_pages) {
        disk_manager_->WritePageAsync(page_id, pages_[frame_id].GetData(), [this, page_id, active_flushes, overall_success, callback](bool success) {
            if (!success) {
                *overall_success = false;
            } else {
                std::lock_guard<std::mutex> guard(latch_);
                auto it2 = page_table_.find(page_id);
                if (it2 != page_table_.end()) {
                    pages_[it2->second].is_dirty_ = false;
                }
            }

            if (--(*active_flushes) == 0) {
                callback(*overall_success);
            }
        });
    }
}

Page *BufferPoolManager::FetchPage(page_id_t page_id) {
    Page *result = nullptr;
    bool done = false;
    FetchPageAsync(page_id, [&](Page *p) {
        result = p;
        done = true;
    });
    while (!done) {
        PollCompletions();
    }
    return result;
}

Page *BufferPoolManager::NewPage(page_id_t *page_id) {
    Page *result = nullptr;
    bool done = false;
    NewPageAsync([&](Page *p) {
        result = p;
        if (p && page_id) {
            *page_id = p->GetPageId();
        }
        done = true;
    });
    while (!done) {
        PollCompletions();
    }
    return result;
}

bool BufferPoolManager::FlushPage(page_id_t page_id) {
    bool res = false;
    bool done = false;
    FlushPageAsync(page_id, [&](bool s) {
        res = s;
        done = true;
    });
    while (!done) {
        PollCompletions();
    }
    return res;
}

bool BufferPoolManager::DeletePage(page_id_t page_id) {
    bool res = false;
    bool done = false;
    DeletePageAsync(page_id, [&](bool s) {
        res = s;
        done = true;
    });
    while (!done) {
        PollCompletions();
    }
    return res;
}
