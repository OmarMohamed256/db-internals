#pragma once

#include <cstdint>
#include <string>
#include "../buffer_pool_manager_skeleton/page.h"

/**
 * BPlusTreePageType distinguishes between internal routing nodes
 * and leaf data nodes.
 */
enum class BPlusTreePageType : int32_t {
    INVALID_INDEX_PAGE = 0,
    LEAF_PAGE = 1,
    INTERNAL_PAGE = 2,
};

/**
 * BPlusTreePage is the base header class for both internal and leaf node pages.
 *
 * Physical Layout on 4096-byte Page:
 * -------------------------------------------------------------
 * | page_type_ (4B) | lsn_ (4B) | size_ (4B) | max_size_ (4B) |
 * | parent_page_id_ (4B) | page_id_ (4B)                       |
 * -------------------------------------------------------------
 * Total Header Size = 24 bytes (strictly 4-byte aligned).
 */
class BPlusTreePage {
public:
    inline bool IsLeafPage() const { return page_type_ == BPlusTreePageType::LEAF_PAGE; }
    inline bool IsRootPage() const { return parent_page_id_ == INVALID_PAGE_ID; }
    inline void SetPageType(BPlusTreePageType type) { page_type_ = type; }

    inline int GetSize() const { return size_; }
    inline void SetSize(int size) { size_ = size; }
    inline void IncreaseSize(int amount) { size_ += amount; }

    inline int GetMaxSize() const { return max_size_; }
    inline void SetMaxSize(int max_size) { max_size_ = max_size; }

    /**
     * MinSize:
     * - Root leaf can have >= 1 entry.
     * - Root internal must have >= 2 pointers (size >= 2).
     * - Non-root internal: ceil(max_size / 2)
     * - Non-root leaf: floor(max_size / 2)
     */
    inline int GetMinSize() const {
        if (IsRootPage()) {
            return IsLeafPage() ? 1 : 2;
        }
        return (max_size_ + 1) / 2;
    }

    inline page_id_t GetParentPageId() const { return parent_page_id_; }
    inline void SetParentPageId(page_id_t parent_page_id) { parent_page_id_ = parent_page_id; }

    inline page_id_t GetPageId() const { return page_id_; }
    inline void SetPageId(page_id_t page_id) { page_id_ = page_id; }

    inline int32_t GetLSN() const { return lsn_; }
    inline void SetLSN(int32_t lsn) { lsn_ = lsn; }

private:
    BPlusTreePageType page_type_{BPlusTreePageType::INVALID_INDEX_PAGE};
    int32_t lsn_{0};                            // Log Sequence Number (for Phase 2.3 WAL & Crash Recovery)
    int size_{0};                               // Number of key/value pairs currently held
    int max_size_{0};                           // Max capacity before a node split is triggered
    page_id_t parent_page_id_{INVALID_PAGE_ID}; // Parent page_id for climbing back up during splits
    page_id_t page_id_{INVALID_PAGE_ID};        // This node's page_id on disk
};

static_assert(sizeof(BPlusTreePage) == 24, "BPlusTreePage header must be exactly 24 bytes");
