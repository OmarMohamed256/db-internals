#pragma once

#include "../storage/rid.h"
#include "../storage/table_heap.h"
#include "../storage/user_row.h"
#include "snapshot.h"
#include "transaction.h"
#include "transaction_manager.h"

/**
 * MVCCTableHeap coordinates multi-version reads, inserts, updates, and deletes
 * on top of a physical TableHeap under Snapshot Isolation.
 */
class MVCCTableHeap {
public:
  MVCCTableHeap(TableHeap *table_heap, TransactionManager *txn_mgr)
      : table_heap_(table_heap), txn_mgr_(txn_mgr) {}

  /**
   * Inserts a new tuple version under the given transaction.
   * Sets version.xmin = txn->GetTxnId(), version.xmax = 0.
   */
  bool Insert(Transaction *txn, const UserRow &row, RID *out_rid) {
    if (txn == nullptr || txn->GetState() != TxnState::IN_PROGRESS) {
      return false;
    }
    UserRow version = row;
    version.xmin = txn->GetTxnId();
    version.xmax = 0;
    version.is_deleted = false;
    return table_heap_->InsertTuple(version, out_rid);
  }

  /**
   * Reads a tuple by RID, returning true only if visible under txn's snapshot.
   */
  bool GetTuple(Transaction *txn, const RID &rid, UserRow *out_row) {
    if (table_heap_->GetTuple(rid, out_row)) {
      bool visible = txn->GetSnapshot().IsVisible(
          *out_row, txn->GetTxnId(),
          [this](txn_id_t id) { return txn_mgr_->GetTxnState(id); });
      return visible;
    }
    return false;
  }

  /**
   * Updates an existing tuple version.
   * Detects write-write conflicts: if old_row.xmax != 0 and active, returns
   * false. Marks old_row.xmax = txn->GetTxnId(), and inserts new version with
   * xmin = txn->GetTxnId().
   */
  bool Update(Transaction *txn, const RID &old_rid, const UserRow &new_row,
              RID *out_new_rid) {
    if (txn == nullptr || txn->GetState() != TxnState::IN_PROGRESS) {
      return false;
    }
    UserRow old_row;
    if (!table_heap_->GetTuple(old_rid, &old_row)) {
      return false;
    }

    // 1. Conflict Check:
    if (old_row.xmax != 0) {
      if (old_row.xmax != txn->GetTxnId() &&
          txn_mgr_->GetTxnState(old_row.xmax) != TxnState::ABORTED) {
        return false; // Active concurrent modifier -> Conflict!
      }
    }

    // 2. Perform out-of-place update (NOT in an else block):
    old_row.xmax = txn->GetTxnId();
    table_heap_->UpdateTuple(old_row, old_rid);

    UserRow new_version = new_row;
    new_version.xmin = txn->GetTxnId();
    new_version.xmax = 0;
    new_version.is_deleted = false;
    return table_heap_->InsertTuple(new_version, out_new_rid);
  }

  /**
   * Logically deletes a tuple version under txn.
   * Detects write-write conflicts, and sets row.xmax = txn->GetTxnId().
   */
  bool Delete(Transaction *txn, const RID &rid) {
    UserRow row;
    table_heap_->GetTuple(rid, &row);
    if (row.xmax != 0) {
      if (row.xmax != txn->GetTxnId() &&
          txn_mgr_->GetTxnState(row.xmax) != TxnState::ABORTED) {
        //  Another concurrent transaction has modified or deleted this row!
        return false;
      }
    }
    row.xmax = txn->GetTxnId();
    return table_heap_->UpdateTuple(row, rid);
  }

  inline TableHeap *GetTableHeap() { return table_heap_; }
  inline TransactionManager *GetTransactionManager() { return txn_mgr_; }

private:
  TableHeap *table_heap_{nullptr};
  TransactionManager *txn_mgr_{nullptr};
};
