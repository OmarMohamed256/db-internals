#pragma once

#include "abstract_executor.h"
#include "../storage/table_heap.h"
#include "../storage/table_iterator.h"
#include "../storage/user_row.h"

/**
 * SeqScanExecutor executes a sequential table scan over an on-disk TableHeap.
 * Located at the bottom (leaves) of the Volcano execution tree.
 *
 * Pulls tuples page-by-page, slot-by-slot using TableIterator, skipping tombstoned
 * records and unpinning pages via RAII ReadPageGuards.
 */
class SeqScanExecutor : public AbstractExecutor<UserRow> {
public:
    explicit SeqScanExecutor(TableHeap *table_heap)
        : table_heap_(table_heap), cursor_(nullptr, RID()) {}

    /**
     * Initializes the sequential scan by positioning the cursor at the first live tuple.
     */
    void Init() override {
        cursor_ = table_heap_->Begin();
    }

    /**
     * Yields the next live tuple from TableHeap.
     * @param[out] out_row Destination pointer for the retrieved UserRow.
     * @param[out] out_rid Destination pointer for the tuple's RID (optional).
     * @return true if a tuple was yielded, false if scan reached EOF.
     */
    bool Next(UserRow *out_row, RID *out_rid) override {
        if(cursor_.IsEnd()){
            return false;
        }
        std::pair<UserRow, RID> pair = (*cursor_); // dereference the cursor
        ++cursor_;
        if (out_row != nullptr) *out_row = pair.first;
        if (out_rid != nullptr) *out_rid = pair.second;
        return true;
    }

private:
    TableHeap *table_heap_{nullptr};
    TableIterator cursor_;
};
