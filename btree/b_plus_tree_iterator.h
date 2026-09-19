#pragma once

#include <utility>
#include "b_plus_tree_leaf_page.h"
#include "../buffer_pool_manager_skeleton/buffer_pool_manager.h"

/**
 * BPlusTreeIterator provides sequential range scan traversal over leaf pages.
 * It leverages the next_page_id_ sibling pointers and ensures proper RAII pin/unpin
 * semantics as it crosses page boundaries.
 */
template <typename KeyType, typename ValueType, typename KeyComparator>
class BPlusTreeIterator {
public:
    using LeafPage = BPlusTreeLeafPage<KeyType, ValueType, KeyComparator>;
    using MappingType = std::pair<KeyType, ValueType>;

    BPlusTreeIterator() = default;

    BPlusTreeIterator(BufferPoolManager *bpm, page_id_t page_id, int index)
        : bpm_(bpm), page_id_(page_id), index_(index) {
        if (page_id_ != INVALID_PAGE_ID && bpm_ != nullptr) {
            page_ = bpm_->FetchPage(page_id_);
        }
    }

    // Direct Page* ownership constructor (zero double-fetch)
    BPlusTreeIterator(BufferPoolManager *bpm, Page *page, int index)
        : bpm_(bpm), page_id_(page != nullptr ? page->GetPageId() : INVALID_PAGE_ID), index_(index), page_(page) {}

    ~BPlusTreeIterator() {
        if (page_ != nullptr && bpm_ != nullptr) {
            bpm_->UnpinPage(page_id_, false);
            page_ = nullptr;
        }
    }

    // Move constructor & assignment
    BPlusTreeIterator(BPlusTreeIterator &&other) noexcept
        : bpm_(other.bpm_), page_id_(other.page_id_), index_(other.index_), page_(other.page_) {
        other.page_ = nullptr;
        other.page_id_ = INVALID_PAGE_ID;
    }

    BPlusTreeIterator &operator=(BPlusTreeIterator &&other) noexcept {
        if (this != &other) {
            if (page_ != nullptr && bpm_ != nullptr) {
                bpm_->UnpinPage(page_id_, false);
            }
            bpm_ = other.bpm_;
            page_id_ = other.page_id_;
            index_ = other.index_;
            page_ = other.page_;
            other.page_ = nullptr;
            other.page_id_ = INVALID_PAGE_ID;
        }
        return *this;
    }

    // Disable copy to prevent double-unpinning of buffer pool frames
    BPlusTreeIterator(const BPlusTreeIterator &) = delete;
    BPlusTreeIterator &operator=(const BPlusTreeIterator &) = delete;

    inline bool IsEnd() const {
        return page_id_ == INVALID_PAGE_ID || page_ == nullptr;
    }

    /**
     * TODO(student): Dereference iterator to return (key, value) pair at current index.
     */
    const MappingType &operator*() const {
        // TODO: Access leaf and return array_[index_]
        auto *leaf = reinterpret_cast<LeafPage*>(page_->GetData());
        return leaf->GetItem(index_);
    }

    /**
     * TODO(student): Advance iterator to the next key-value pair.
     *
     * Steps:
     * 1. If IsEnd(), return *this.
     * 2. Increment index_.
     * 3. If index_ >= leaf->GetSize():
     *    - Read next_page_id = leaf->GetNextPageId().
     *    - Unpin current page: bpm_->UnpinPage(page_id_, false).
     *    - Set page_ = nullptr, page_id_ = next_page_id, index_ = 0.
     *    - If page_id_ != INVALID_PAGE_ID, fetch next page: page_ = bpm_->FetchPage(page_id_).
     * 4. Return *this.
     */
    BPlusTreeIterator &operator++() {
        if (IsEnd()) return *this;
        auto *leaf = reinterpret_cast<LeafPage *>(page_->GetData());
        index_++;
        if (index_ >= leaf->GetSize()) {
            page_id_t next_page_id = leaf->GetNextPageId();
            bpm_->UnpinPage(page_id_, false);
            page_ = nullptr;
            page_id_ = next_page_id;
            index_ = 0;
            if (page_id_ != INVALID_PAGE_ID) {
                page_ = bpm_->FetchPage(page_id_);
            }
        }
        return *this;
    }

    bool operator==(const BPlusTreeIterator &other) const {
        if (IsEnd() && other.IsEnd()) {
            return true;
        }
        return page_id_ == other.page_id_ && index_ == other.index_;
    }

bool operator!=(const BPlusTreeIterator &other) const {
        return !(*this == other);
    }

private:
    BufferPoolManager *bpm_{nullptr};
    page_id_t page_id_{INVALID_PAGE_ID};
    int index_{0};
    Page *page_{nullptr};
};
