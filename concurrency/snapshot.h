#pragma once

#include "../storage/user_row.h"
#include <cstdint>
#include <functional>
#include <unordered_set>

using txn_id_t = int64_t;
static constexpr txn_id_t INVALID_TXN_ID = 0;

/**
 * TxnState represents the lifecycle status of a database transaction.
 */
enum class TxnState { IN_PROGRESS, COMMITTED, ABORTED };

/**
 * Snapshot captures the point-in-time visibility boundary for a transaction:
 * - read_ts: The transaction ID / logical timestamp when the snapshot was
 * established.
 * - active_txns: The set of transaction IDs that were actively IN_PROGRESS at
 * read_ts.
 */
struct Snapshot {
  txn_id_t read_ts{0};
  std::unordered_set<txn_id_t> active_txns;

  Snapshot() = default;
  Snapshot(txn_id_t read_ts, std::unordered_set<txn_id_t> active)
      : read_ts(read_ts), active_txns(std::move(active)) {}

  /**
   * PostgreSQL-style MVCC Tuple Visibility Evaluation (HeapTupleSatisfiesMVCC).
   *
   * Evaluates whether a UserRow is visible to a transaction with this snapshot.
   *
   * @param row The tuple being inspected (with row.xmin and row.xmax).
   * @param current_txn_id The transaction ID of the reader evaluating
   * visibility.
   * @param get_txn_state Callback retrieving the current state of any
   * transaction ID.
   * @return true if the tuple is visible under this snapshot, false otherwise.
   */
  bool IsVisible(const UserRow &row, txn_id_t current_txn_id,
                 const std::function<TxnState(txn_id_t)> &get_txn_state) const {
    // -------------------------------------------------------------
    // Phase 1: Self-Modifications & Creator (xmin) Evaluation
    // -------------------------------------------------------------
    if (row.xmin == current_txn_id) {
      // Rules 1 & 3: Own insert visible unless deleted by self
      return row.xmax != current_txn_id;
    }

    // Rules 5, 6, 7: Active, future, or aborted creator is invisible
    if (active_txns.find(row.xmin) != active_txns.end())
      return false;
    if (row.xmin > read_ts)
      return false;
    if (get_txn_state(row.xmin) != TxnState::COMMITTED)
      return false;

    // -------------------------------------------------------------
    // Phase 2: Deleter (xmax) Evaluation
    // -------------------------------------------------------------
    if (row.xmax == 0)
      return true; // Rule 8: Never deleted
    if (row.xmax == current_txn_id)
      return false; // Rule 2: Deleted by self

    // Rules 10 & 11: Active, future, or aborted deleter -> still alive
    if (active_txns.find(row.xmax) != active_txns.end())
      return true;
    if (row.xmax > read_ts)
      return true;
    if (get_txn_state(row.xmax) == TxnState::ABORTED)
      return true;

    // Rule 9: Committed deleter before snapshot -> dead
    if (get_txn_state(row.xmax) == TxnState::COMMITTED)
      return false;

    return true;
  }
};
