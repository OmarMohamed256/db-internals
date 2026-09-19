#pragma once

#include "abstract_executor.h"
#include "../btree/b_plus_tree.h"
#include "../storage/table_heap.h"
#include "../storage/user_row.h"
#include <utility>

/**
 * IndexScanExecutor executes an index scan over a B+ Tree index,
 * dereferencing retrieved RIDs against TableHeap in O(log N) time.
 * Located at the bottom (leaves) of the Volcano execution tree.
 *
 * Supports:
 * 1. Point Lookups: low_key == high_key (e.g. SELECT * FROM users WHERE id = 42)
 * 2. Range Scans: [low_key, high_key] (e.g. SELECT * FROM users WHERE id BETWEEN 100 AND 200)
 */
class IndexScanExecutor : public AbstractExecutor<UserRow> {
public:
    using TreeType = BPlusTree<int64_t, RID>;

    /**
     * Range scan constructor: scans keys in [low_key, high_key] inclusive.
     */
    IndexScanExecutor(TreeType *index, TableHeap *table_heap, int64_t low_key, int64_t high_key)
        : index_(index), table_heap_(table_heap), low_key_(low_key), high_key_(high_key) {}

    /**
     * Point lookup convenience constructor: scans a single key.
     */
    IndexScanExecutor(TreeType *index, TableHeap *table_heap, int64_t key)
        : IndexScanExecutor(index, table_heap, key, key) {}

    /**
     * Positions the B+ Tree iterator at the first entry >= low_key.
     */
    void Init() override {
        cursor_ = index_->Begin(low_key_);
    }

    /**
     * Yields the next tuple in range [low_key_, high_key_].
     * @param[out] out_row Destination pointer for the retrieved UserRow.
     * @param[out] out_rid Destination pointer for the tuple's RID (optional).
     * @return true if a live tuple was found and yielded, false if range exhausted.
     */
    bool Next(UserRow *out_row, RID *out_rid) override {
        while (!cursor_.IsEnd()) {
            auto [key, rid] = *cursor_;
            if(key > high_key_) return false;
            ++cursor_;
            // getting the actual tuple from the page
            // process: fetch page from bpm that fetches it from disk manager if it doesnt exist
            // then we ask the page manager to use slotted array to get the tuple offset
            // and then translate into the actual materalized tuple
            // only disk is async for now (i dont know if we should make upper levels async or not)
            // since now we are blocking on upper levels in the thread ??? not sure
            bool found = table_heap_->GetTuple(rid, out_row);
            if(found){
                if(out_rid != nullptr){
                    *out_rid = rid;
                    return true;
                }
            }
        }
        return false;
    }

private:
    TreeType *index_{nullptr};
    TableHeap *table_heap_{nullptr};
    int64_t low_key_{0};
    int64_t high_key_{0};
    TreeType::Iterator cursor_{};
};
