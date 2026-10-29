#pragma once

#include <atomic>
#include <cassert>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

#include "snapshot.h"
#include "transaction.h"

/**
 * TransactionManager coordinates transaction lifecycles, assigns monotonic
 * transaction identifiers, establishes visibility snapshots, and tracks active
 * transactions.
 */
class TransactionManager {
public:
  TransactionManager() = default;

  ~TransactionManager() {
    std::lock_guard<std::mutex> lock(latch_);
    for (auto &pair : allocated_txns_) {
      delete pair.second;
    }
    allocated_txns_.clear();
  }

  /**
   * Begins a new transaction with Snapshot Isolation.
   * Assigns a monotonic txn_id, captures active transaction set, and returns
   * Transaction*.
   */
  Transaction *
  Begin(IsolationLevel level = IsolationLevel::SNAPSHOT_ISOLATION) {
    txn_id_t xid = next_txn_id_++;
    std::lock_guard<std::mutex> lock(latch_);
    Snapshot snapshot(xid, active_txns_);
    txn_states_[xid] = TxnState::IN_PROGRESS;
    active_txns_.insert(xid);
    Transaction *txn = new Transaction(xid, std::move(snapshot), level);
    allocated_txns_[xid] = txn;
    return txn;
  }

  /**
   * Commits the given transaction.
   * Marks state as COMMITTED and removes txn_id from active_txns_.
   */
  void Commit(Transaction *txn) {
    if (txn == nullptr || txn->GetState() != TxnState::IN_PROGRESS)
      return;
    std::lock_guard<std::mutex> lock(latch_);
    txn_states_[txn->GetTxnId()] = TxnState::COMMITTED;
    active_txns_.erase(txn->GetTxnId());
    txn->SetState(TxnState::COMMITTED);
    return;
  }

  /**
   * Aborts the given transaction.
   * Marks state as ABORTED and removes txn_id from active_txns_.
   */
  void Abort(Transaction *txn) {
    if (txn == nullptr)
      return;
    std::lock_guard<std::mutex> lock(latch_);
    txn_states_[txn->GetTxnId()] = TxnState::ABORTED;
    active_txns_.erase(txn->GetTxnId());
    txn->SetState(TxnState::ABORTED);
    return;
  }

  /**
   * Returns the current state of a transaction ID.
   */
  TxnState GetTxnState(txn_id_t txn_id) {
    std::lock_guard<std::mutex> lock(latch_);
    auto it = txn_states_.find(txn_id);
    if (it == txn_states_.end()) {
      return TxnState::ABORTED;
    }
    return it->second;
  }

  inline size_t GetActiveTxnCount() {
    std::lock_guard<std::mutex> lock(latch_);
    return active_txns_.size();
  }

  inline txn_id_t GetNextTxnId() const { return next_txn_id_.load(); }

private:
  std::atomic<txn_id_t> next_txn_id_{1};
  std::mutex latch_;
  std::unordered_map<txn_id_t, TxnState> txn_states_;
  std::unordered_set<txn_id_t> active_txns_;
  std::unordered_map<txn_id_t, Transaction *> allocated_txns_;
};
