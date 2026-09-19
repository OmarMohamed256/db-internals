#pragma once

#include <utility>
#include "../buffer_pool_manager_skeleton/buffer_pool_manager.h"
#include "../buffer_pool_manager_skeleton/page.h"

class ReadPageGuard;
class WritePageGuard;

/**
 * BasicPageGuard: RAII wrapper around a buffer pool page.
 * 
 * Ensures that whenever a page is fetched from the BufferPoolManager, its pin count
 * is automatically decremented when the guard falls out of scope.
 * 
 * Invariants to enforce:
 * - Move-only semantics (no copies allowed).
 * - Dropping unpins the page with `is_dirty_` if valid.
 * - Moving transfers ownership and clears the source guard (page_ = nullptr, bpm_ = nullptr).
 */
class BasicPageGuard {
public:
    BasicPageGuard() = default;

    BasicPageGuard(BufferPoolManager *bpm, Page *page)
        : bpm_(bpm), page_(page), is_dirty_(false) {}

    ~BasicPageGuard() {
        Drop();
    }

    // Move constructor: transfers ownership and clears source
    BasicPageGuard(BasicPageGuard &&that) noexcept
        : bpm_(that.bpm_), page_(that.page_), is_dirty_(that.is_dirty_) {
        that.bpm_ = nullptr;
        that.page_ = nullptr;
        that.is_dirty_ = false;
    }

    // Move assignment operator: drops current page and transfers from source
    BasicPageGuard &operator=(BasicPageGuard &&that) noexcept {
        if (this != &that) {
            Drop();
            bpm_ = that.bpm_;
            page_ = that.page_;
            is_dirty_ = that.is_dirty_;
            that.bpm_ = nullptr;
            that.page_ = nullptr;
            that.is_dirty_ = false;
        }
        return *this;
    }

    // Delete copy operations to prevent double unpinning
    BasicPageGuard(const BasicPageGuard &) = delete;
    BasicPageGuard &operator=(const BasicPageGuard &) = delete;

    void Drop() {
        if (page_ != nullptr && bpm_ != nullptr) {
            bpm_->UnpinPage(page_->GetPageId(), is_dirty_);
        }
        page_ = nullptr;
        bpm_ = nullptr;
        is_dirty_ = false;
    }

    inline page_id_t PageId() const {
        return page_ != nullptr ? page_->GetPageId() : INVALID_PAGE_ID;
    }

    inline const char *GetData() const {
        return page_ != nullptr ? page_->GetData() : nullptr;
    }

    inline char *GetDataMut() {
        is_dirty_ = true;
        return page_ != nullptr ? page_->GetData() : nullptr;
    }

    template <typename T>
    inline const T *As() const {
        return reinterpret_cast<const T *>(GetData());
    }

    template <typename T>
    inline T *AsMut() {
        return reinterpret_cast<T *>(GetDataMut());
    }

    inline Page *GetPage() { return page_; }
    inline const Page *GetPage() const { return page_; }
    inline BufferPoolManager *GetBPM() { return bpm_; }

    inline void SetDirty(bool is_dirty) { is_dirty_ = is_dirty; }
    inline bool IsDirty() const { return is_dirty_; }
    inline bool IsValid() const { return page_ != nullptr; }
    inline explicit operator bool() const { return IsValid(); }

    ReadPageGuard UpgradeRead();
    WritePageGuard UpgradeWrite();

private:
    BufferPoolManager *bpm_{nullptr};
    Page *page_{nullptr};
    bool is_dirty_{false};

    friend class ReadPageGuard;
    friend class WritePageGuard;
};

/**
 * ReadPageGuard: RAII wrapper for concurrent read access.
 * 
 * Invariants to enforce:
 * - Acquires page->RLatch() on construction if valid.
 * - Releases page->RUnlatch() on destruction before unpinning via guard_.Drop().
 */
class ReadPageGuard {
public:
    ReadPageGuard() = default;

    ReadPageGuard(BufferPoolManager *bpm, Page *page) : guard_(bpm, page) {
        if (guard_.GetPage() != nullptr) {
            guard_.GetPage()->RLatch();
        }
    }

    explicit ReadPageGuard(BasicPageGuard &&guard) : guard_(std::move(guard)) {
        if (guard_.GetPage() != nullptr) {
            guard_.GetPage()->RLatch();
        }
    }

    ~ReadPageGuard() {
        Drop();
    }

    ReadPageGuard(ReadPageGuard &&that) noexcept = default;

    ReadPageGuard &operator=(ReadPageGuard &&that) noexcept {
        if (this != &that) {
            Drop();
            guard_ = std::move(that.guard_);
        }
        return *this;
    }

    ReadPageGuard(const ReadPageGuard &) = delete;
    ReadPageGuard &operator=(const ReadPageGuard &) = delete;

    void Drop() {
        if (guard_.GetPage() != nullptr) {
            guard_.GetPage()->RUnlatch();
            guard_.Drop();
        }
    }

    inline page_id_t PageId() const { return guard_.PageId(); }
    inline const char *GetData() const { return guard_.GetData(); }

    template <typename T>
    inline const T *As() const { return guard_.As<T>(); }

    inline Page *GetPage() { return guard_.GetPage(); }
    inline const Page *GetPage() const { return guard_.GetPage(); }

    inline bool IsValid() const { return guard_.IsValid(); }
    inline explicit operator bool() const { return IsValid(); }

private:
    BasicPageGuard guard_;
};

/**
 * WritePageGuard: RAII wrapper for exclusive write access.
 * 
 * Invariants to enforce:
 * - Acquires page->WLatch() and marks dirty on construction if valid.
 * - Releases page->WUnlatch() on destruction before unpinning via guard_.Drop().
 */
class WritePageGuard {
public:
    WritePageGuard() = default;

    WritePageGuard(BufferPoolManager *bpm, Page *page) : guard_(bpm, page) {
        if (guard_.GetPage() != nullptr) {
            guard_.GetPage()->WLatch();
            guard_.SetDirty(true);
        }
    }

    explicit WritePageGuard(BasicPageGuard &&guard) : guard_(std::move(guard)) {
        if (guard_.GetPage() != nullptr) {
            guard_.GetPage()->WLatch();
            guard_.SetDirty(true);
        }
    }

    ~WritePageGuard() {
        Drop();
    }

    WritePageGuard(WritePageGuard &&that) noexcept = default;

    WritePageGuard &operator=(WritePageGuard &&that) noexcept {
        if (this != &that) {
            Drop();
            guard_ = std::move(that.guard_);
        }
        return *this;
    }

    WritePageGuard(const WritePageGuard &) = delete;
    WritePageGuard &operator=(const WritePageGuard &) = delete;

    void Drop() {
        if (guard_.GetPage() != nullptr) {
            guard_.GetPage()->WUnlatch();
            guard_.Drop();
        }
    }

    inline page_id_t PageId() const { return guard_.PageId(); }
    inline const char *GetData() const { return guard_.GetData(); }
    inline char *GetDataMut() { return guard_.GetDataMut(); }

    template <typename T>
    inline const T *As() const { return guard_.As<T>(); }

    template <typename T>
    inline T *AsMut() { return guard_.AsMut<T>(); }

    inline Page *GetPage() { return guard_.GetPage(); }
    inline const Page *GetPage() const { return guard_.GetPage(); }

    inline void SetDirty(bool is_dirty) { guard_.SetDirty(is_dirty); }
    inline bool IsValid() const { return guard_.IsValid(); }
    inline explicit operator bool() const { return IsValid(); }

private:
    BasicPageGuard guard_;
};

inline ReadPageGuard BasicPageGuard::UpgradeRead() {
    return ReadPageGuard(std::move(*this));
}

inline WritePageGuard BasicPageGuard::UpgradeWrite() {
    return WritePageGuard(std::move(*this));
}

// ============================================================================
// Ergonomic Helper Functions for BufferPoolManager
// ============================================================================

inline BasicPageGuard FetchPageBasic(BufferPoolManager *bpm, page_id_t page_id) {
    if (bpm == nullptr || page_id == INVALID_PAGE_ID) {
        return BasicPageGuard();
    }
    Page *page = bpm->FetchPage(page_id);
    return BasicPageGuard(bpm, page);
}

inline ReadPageGuard FetchPageRead(BufferPoolManager *bpm, page_id_t page_id) {
    if (bpm == nullptr || page_id == INVALID_PAGE_ID) {
        return ReadPageGuard();
    }
    Page *page = bpm->FetchPage(page_id);
    return ReadPageGuard(bpm, page);
}

inline WritePageGuard FetchPageWrite(BufferPoolManager *bpm, page_id_t page_id) {
    if (bpm == nullptr || page_id == INVALID_PAGE_ID) {
        return WritePageGuard();
    }
    Page *page = bpm->FetchPage(page_id);
    return WritePageGuard(bpm, page);
}

inline BasicPageGuard NewPageBasic(BufferPoolManager *bpm, page_id_t *page_id) {
    if (bpm == nullptr) {
        return BasicPageGuard();
    }
    Page *page = bpm->NewPage(page_id);
    return BasicPageGuard(bpm, page);
}

inline WritePageGuard NewPageWrite(BufferPoolManager *bpm, page_id_t *page_id) {
    if (bpm == nullptr) {
        return WritePageGuard();
    }
    Page *page = bpm->NewPage(page_id);
    return WritePageGuard(bpm, page);
}
