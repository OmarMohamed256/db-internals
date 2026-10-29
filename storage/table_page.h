#pragma once

#include "../buffer_pool_manager_skeleton/page.h"
#include "rid.h"
#include "user_row.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>

/**
 * TableSlot represents an entry in the slotted-page slot directory.
 */
struct TableSlot {
  uint32_t offset{0};  // Offset from start of page data to tuple bytes
  std::size_t size{0}; // Byte size of tuple (0 indicates empty or tombstoned)
};

/**
 * TablePage implements a Slotted-Page frame format (CMU 15-445 Lecture #03 &
 * #04)
 *
 * Physical Layout (4096 bytes):
 * +-------------------------------------------------------------------+
 * | Header (32 bytes):                                                |
 * |   lsn (8B) | page_id (4B) | prev_page_id (4B) | next_page_id (4B)  |
 * |   num_records (4B) | free_space_ptr (4B) | flags (4B)             |
 * +-------------------------------------------------------------------+
 * | Slot Directory (growing downwards towards bottom):                |
 * |   Slot 0 {offset, size}                                           |
 * |   Slot 1 {offset, size}                                           |
 * |   ...                                                             |
 * |   Slot N {offset, size}                                           |
 * +-------------------------------------------------------------------+
 * |                     FREE SPACE GAP                                |
 * +-------------------------------------------------------------------+
 * | Tuple Storage (growing upwards from end of page):                 |
 * |   ...                                                             |
 * |   Tuple 1 [UserRow bytes]                                         |
 * |   Tuple 0 [UserRow bytes]                                         |
 * +-------------------------------------------------------------------+
 */
class TablePage {
public:
  static constexpr size_t TABLE_PAGE_HEADER_SIZE = 32;

  void Init(page_id_t page_id, page_id_t prev_page_id = INVALID_PAGE_ID,
            page_id_t next_page_id = INVALID_PAGE_ID) {
    page_id_ = page_id, prev_page_id_ = prev_page_id,
    next_page_id_ = next_page_id;
    num_records_ = 0;
    free_space_ptr_ = PAGE_SIZE;
    lsn_ = 0, flags_ = 0;
    std::memset(GetData() + TABLE_PAGE_HEADER_SIZE, 0,
                PAGE_SIZE - TABLE_PAGE_HEADER_SIZE);
  }

  inline page_id_t GetPageId() const { return page_id_; }
  inline page_id_t GetPrevPageId() const { return prev_page_id_; }
  inline void SetPrevPageId(page_id_t prev_id) { prev_page_id_ = prev_id; }
  inline page_id_t GetNextPageId() const { return next_page_id_; }
  inline void SetNextPageId(page_id_t next_id) { next_page_id_ = next_id; }
  inline uint32_t GetNumRecords() const { return num_records_; }
  inline uint64_t GetLSN() const { return lsn_; }
  inline void SetLSN(uint64_t lsn) { lsn_ = lsn; }

  inline char *GetData() { return reinterpret_cast<char *>(this); }
  inline const char *GetData() const {
    return reinterpret_cast<const char *>(this);
  }

  inline TableSlot *GetSlots() {
    return reinterpret_cast<TableSlot *>(GetData() + TABLE_PAGE_HEADER_SIZE);
  }

  inline const TableSlot *GetSlots() const {
    return reinterpret_cast<const TableSlot *>(GetData() +
                                               TABLE_PAGE_HEADER_SIZE);
  }

  inline size_t GetSlotDirectoryEndOffset() const {
    return TABLE_PAGE_HEADER_SIZE + num_records_ * sizeof(TableSlot);
  }

  inline size_t GetFreeSpaceRemaining() const {
    size_t slot_dir_end = GetSlotDirectoryEndOffset();
    if (free_space_ptr_ < slot_dir_end) {
      return 0;
    }
    return free_space_ptr_ - slot_dir_end;
  }

  /**
   * Insert a UserRow into this slotted page.
   */
  bool InsertTuple(const UserRow &row, RID *out_rid) {
    for (uint32_t slot_idx = 0; slot_idx < num_records_; slot_idx++) {
      if (GetSlots()[slot_idx].size == 0) {
        std::memcpy(GetData() + GetSlots()[slot_idx].offset, &row, sizeof(UserRow));
        GetSlots()[slot_idx].size = sizeof(UserRow);
        if (out_rid != nullptr) {
          *out_rid = {page_id_, slot_idx};
        }
        return true;
      }
    }

    if (GetFreeSpaceRemaining() >= sizeof(UserRow) + sizeof(TableSlot)) {
      free_space_ptr_ -= sizeof(UserRow);
      std::memcpy(GetData() + free_space_ptr_, &row, sizeof(UserRow));
      uint32_t slot_idx = num_records_++;
      GetSlots()[slot_idx] = {static_cast<uint16_t>(free_space_ptr_), sizeof(UserRow)};
      if (out_rid != nullptr) {
        *out_rid = {page_id_, slot_idx};
      }
      return true;
    }
    return false;
  }

  /**
   * Retrieve tuple by slot number.
   */
  bool GetTuple(uint32_t slot_num, UserRow *out_row) const {
    if (slot_num < num_records_ && GetSlots()[slot_num].size > 0) {
      // Copy FROM page memory INTO out_row
      std::memcpy(out_row, GetData() + GetSlots()[slot_num].offset,
                  sizeof(UserRow));

      // Ensure logically deleted tuples are not returned
      if (out_row->is_deleted) {
        return false;
      }
      return true;
    }
    return false;
  }

  /**
   * Update tuple at slot number in-place.
   */
  bool UpdateTuple(uint32_t slot_num, const UserRow &new_row) {
    if (slot_num >= num_records_ || GetSlots()[slot_num].size == 0) {
      return false;
    }
    std::memcpy(GetData() + GetSlots()[slot_num].offset, &new_row, sizeof(UserRow));
    return true;
  }

  /**
   * Mark tuple as tombstoned/deleted.
   */
  bool MarkDelete(uint32_t slot_num) {
    if (slot_num >= num_records_ || GetSlots()[slot_num].size == 0) {
      return false;
    }
    auto *row = reinterpret_cast<UserRow *>(GetData() + GetSlots()[slot_num].offset);
    row->is_deleted = true;
    GetSlots()[slot_num].size = 0; // Mark slot available for reuse
    return true;
  }

  /**
   * Count live (non-deleted) records currently residing on this page.
   */
  uint32_t GetLiveRecordCount() const {
    uint32_t live = 0;
    for (uint32_t i = 0; i < num_records_; ++i) {
      if (GetSlots()[i].size > 0) {
        const auto *row = reinterpret_cast<const UserRow *>(GetData() + GetSlots()[i].offset);
        if (!row->is_deleted) {
          live++;
        }
      }
    }
    return live;
  }

private:
  uint64_t lsn_{0};                         // 8B offset 0..7 (WAL LSN)
  page_id_t page_id_{INVALID_PAGE_ID};      // 4B offset 8..11
  page_id_t prev_page_id_{INVALID_PAGE_ID}; // 4B offset 12..15
  page_id_t next_page_id_{INVALID_PAGE_ID}; // 4B offset 16..19
  uint32_t num_records_{0};                 // 4B offset 20..23
  uint32_t free_space_ptr_{PAGE_SIZE};      // 4B offset 24..27
  uint32_t flags_{0};                       // 4B offset 28..31
};

static_assert(
    sizeof(TablePage) == TablePage::TABLE_PAGE_HEADER_SIZE,
    "TablePage header size must match TABLE_PAGE_HEADER_SIZE (32 bytes)");
