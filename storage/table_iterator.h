#pragma once

#include "rid.h"
#include "user_row.h"

class TableHeap;

/**
 * TableIterator provides forward cursor traversal over all live tuples
 * stored across the chained pages of a TableHeap.
 * 
 * This iterator directly powers Volcano SeqScanExecutor in Sprint 1.3.
 */
class TableIterator {
public:
    TableIterator(TableHeap *table_heap, RID rid);

    bool operator==(const TableIterator &other) const {
        return rid_ == other.rid_;
    }

    bool operator!=(const TableIterator &other) const {
        return !(*this == other);
    }

    inline bool IsEnd() const {
        return !rid_.IsValid();
    }

    std::pair<UserRow, RID> operator*();
    TableIterator &operator++();

    RID GetRID() const { return rid_; }

private:
    void AdvanceToNextLiveTuple();

    TableHeap *table_heap_{nullptr};
    RID rid_{INVALID_PAGE_ID, 0};
    UserRow current_row_{};
};
