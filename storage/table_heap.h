#pragma once

#include "../buffer_pool_manager_skeleton/buffer_pool_manager.h"
#include "page_guard.h"
#include "rid.h"
#include "table_iterator.h"
#include "table_page.h"
#include "user_row.h"
#include <cassert>
#include <mutex>

/**
 * TableHeap manages a doubly-linked collection of 4KB TablePages in the
 * BufferPoolManager.
 *
 * In standard disk-oriented database architectures (CMU 15-445):
 * - TableHeap is the authoritative physical storage location for records.
 * - Primary and secondary indexes store (Key -> RID) pairs pointing into
 * TableHeap.
 * - Page allocation, unpinning, and latching are safely encapsulated using RAII
 * PageGuards.
 */
class TableHeap {
public:
  // Create a new TableHeap and allocate its root/first TablePage
  explicit TableHeap(BufferPoolManager *bpm);

  // Open an existing TableHeap starting at first_page_id
  TableHeap(BufferPoolManager *bpm, page_id_t first_page_id);

  ~TableHeap() = default;

  /**
   * Insert a UserRow into the table heap.
   * Appends to the tail page or allocates a newly chained TablePage if full.
   */
  bool InsertTuple(const UserRow &row, RID *out_rid);

  /**
   * Retrieve tuple by RID using ReadPageGuard.
   * Returns false if slot is invalid, deleted, or out of range.
   */
  bool GetTuple(const RID &rid, UserRow *out_row);

  /**
   * Update an existing tuple in-place using WritePageGuard.
   */
  bool UpdateTuple(const UserRow &row, const RID &rid);

  /**
   * Mark a tuple as logically deleted using WritePageGuard.
   */
  bool MarkDelete(const RID &rid);

  /**
   * Table scan iterators for Volcano SeqScan.
   */
  TableIterator Begin();
  TableIterator End();

  inline page_id_t GetFirstPageId() const { return first_page_id_; }
  inline BufferPoolManager *GetBPM() { return bpm_; }

  // Statistics used by the Cost-Based Optimizer (Sprint 1.4)
  size_t GetTotalPageCount();
  size_t GetTotalRowCount();

private:
  BufferPoolManager *bpm_{nullptr};
  page_id_t first_page_id_{INVALID_PAGE_ID};
  page_id_t last_page_id_{INVALID_PAGE_ID};
  std::mutex latch_; // Protects multi-page tail allocation & page chaining
};
