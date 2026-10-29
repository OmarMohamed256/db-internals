#pragma once

#include "../execution/abstract_executor.h"
#include "../storage/table_heap.h"
#include "../storage/table_iterator.h"
#include "../storage/user_row.h"
#include "mvcc_table_heap.h"
#include "transaction.h"
#include "transaction_manager.h"

/**
 * MVCCSeqScanExecutor executes a non-blocking sequential table scan
 * filtered by a transaction's Snapshot Isolation visibility boundary.
 *
 * Implements the Volcano pull-based iterator interface (Sprint 1.3).
 * Readers never acquire shared read locks and never block concurrent writers.
 */
class MVCCSeqScanExecutor : public AbstractExecutor<UserRow> {
public:
  MVCCSeqScanExecutor(MVCCTableHeap *mvcc_heap, Transaction *txn)
      : mvcc_heap_(mvcc_heap), txn_(txn), cursor_(nullptr, RID()) {}

  void Init() override {
    if (mvcc_heap_ != nullptr && mvcc_heap_->GetTableHeap() != nullptr) {
      cursor_ = mvcc_heap_->GetTableHeap()->Begin();
    }
  }

  bool Next(UserRow *out_row, RID *out_rid) override {
    while (!cursor_.IsEnd()) {
      auto [row, rid] = *cursor_;
      ++cursor_;
      bool visible = txn_->GetSnapshot().IsVisible(
          row, txn_->GetTxnId(), [this](txn_id_t id) {
            return mvcc_heap_->GetTransactionManager()->GetTxnState(id);
          });
      if (visible) {
        if (out_row != nullptr)
          *out_row = row;
        if (out_rid != nullptr)
          *out_rid = rid;
        return true;
      }
    }
    return false;
  }

private:
  MVCCTableHeap *mvcc_heap_{nullptr};
  Transaction *txn_{nullptr};
  TableIterator cursor_;
};
