#pragma once

#include <cstdint>
#include <unordered_set>
#include <iostream>
#include "snapshot.h"

/**
 * IsolationLevel defines the concurrency guarantees provided to transactions.
 */
enum class IsolationLevel {
    READ_COMMITTED,
    REPEATABLE_READ,
    SNAPSHOT_ISOLATION
};

/**
 * Transaction encapsulates transaction state, identifiers, and visibility snapshot.
 */
class Transaction {
public:
    explicit Transaction(txn_id_t txn_id, Snapshot snapshot,
                         IsolationLevel isolation_level = IsolationLevel::SNAPSHOT_ISOLATION)
        : txn_id_(txn_id), read_ts_(snapshot.read_ts),
          state_(TxnState::IN_PROGRESS), isolation_level_(isolation_level),
          snapshot_(std::move(snapshot)) {}

    inline txn_id_t GetTxnId() const { return txn_id_; }
    inline txn_id_t GetReadTs() const { return read_ts_; }
    inline TxnState GetState() const { return state_; }
    inline IsolationLevel GetIsolationLevel() const { return isolation_level_; }
    inline const Snapshot &GetSnapshot() const { return snapshot_; }

    inline void SetState(TxnState new_state) { state_ = new_state; }

private:
    txn_id_t txn_id_{INVALID_TXN_ID};
    txn_id_t read_ts_{0};
    TxnState state_{TxnState::IN_PROGRESS};
    IsolationLevel isolation_level_{IsolationLevel::SNAPSHOT_ISOLATION};
    Snapshot snapshot_;
};
