#pragma once

#include <unordered_map>
#include <vector>
#include "abstract_executor.h"
#include "order_row.h"
#include "../storage/user_row.h"

/**
 * HashJoinExecutor implements an in-memory hash join between
 * an outer table (UserRow) and an inner table (OrderRow).
 *
 * Equi-join condition: users.id == orders.user_id
 * Output Schema: JoinedUserOrderRow
 *
 * The Hash Join algorithm operates in two distinct phases:
 *
 * 1. Build Phase (Pipeline Breaker):
 *    Executed entirely inside Init().
 *    Drains the build relation (e.g. outer UserRow table) into an in-memory hash table:
 *      hash_table_[user.id] = user;
 *    Because this must consume the entire build relation before producing a single
 *    output tuple, it is called a "pipeline breaker".
 *
 * 2. Probe Phase (Streaming):
 *    Executed in Next().
 *    Pulls rows one-by-one from the probe relation (e.g. inner OrderRow table).
 *    For each order, probes the hash table using order.user_id in O(1) time.
 *    Emits matching JoinedUserOrderRow(matched_user, order).
 *
 * Algorithmic Complexity:
 * - Time: O(|Outer| + |Inner|) - Linear Time! Eliminates the O(|Outer| * |Inner|) quadratic blowup.
 * - Space: O(|Build Table|) auxiliary memory.
 */
class HashJoinExecutor : public AbstractExecutor<JoinedUserOrderRow> {
public:
    HashJoinExecutor(AbstractExecutor<UserRow> *outer_child,
                     AbstractExecutor<OrderRow> *inner_child)
        : outer_child_(outer_child), inner_child_(inner_child) {}

    /**
     * Executes the Build Phase:
     * Drains outer_child_ completely and constructs the in-memory hash table.
     * Initializes inner_child_ for streaming probe.
     */
    void Init() override {
        // TODO(student):
        // 1. Clear hash_table_ to support re-initialization.
        // 2. Initialize outer_child_->Init().
        // 3. Loop pulling all rows from outer_child_:
        //      UserRow user;
        //      RID rid;
        //      while (outer_child_->Next(&user, &rid)) {
        //          hash_table_[user.id] = user;
        //      }
        // 4. Initialize inner_child_->Init() so probe phase is ready to stream.
        hash_table_.clear();
        outer_child_->Init();
        UserRow user;
        RID rid;
        while (outer_child_->Next(&user, &rid)) {
            hash_table_[user.id] = user;
        }
        inner_child_->Init();
    }

    /**
     * Executes the Probe Phase:
     * Pulls the next record from inner_child_, probes hash_table_, and yields on match.
     * @param[out] out_row Destination pointer for the joined record.
     * @param[out] out_rid Optional RID destination pointer.
     * @return true if a joined record was produced, false if probe relation reached EOF.
     */
    bool Next(JoinedUserOrderRow *out_row, RID *out_rid) override {
        // TODO(student):
        // 1. Loop while inner_child_->Next(&curr_order_, &curr_order_rid_):
        //    a. Look up curr_order_.user_id in hash_table_.
        //    b. If found:
        //         *out_row = JoinedUserOrderRow(it->second, curr_order_);
        //         if (out_rid != nullptr) *out_rid = curr_order_rid_;
        //         return true;
        // 2. When inner_child_ is exhausted, return false.
        while (inner_child_->Next(&curr_order_, &curr_order_rid_)) {
            auto it = hash_table_.find(curr_order_.user_id);
            if (it != hash_table_.end()) {
                if (out_row != nullptr) {
                    *out_row = JoinedUserOrderRow(it->second, curr_order_);
                }
                if (out_rid != nullptr) {
                    *out_rid = curr_order_rid_;
                }
                return true;
            }
        }
        return false;
    }

    inline size_t GetHashTableSize() const { return hash_table_.size(); }

private:
    AbstractExecutor<UserRow> *outer_child_{nullptr};
    AbstractExecutor<OrderRow> *inner_child_{nullptr};

    // Build Table: User PK id -> UserRow
    std::unordered_map<int64_t, UserRow> hash_table_;

    OrderRow curr_order_{};
    RID curr_order_rid_{};
};
