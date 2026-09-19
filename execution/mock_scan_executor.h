#pragma once

#include <vector>
#include <utility>
#include "abstract_executor.h"

/**
 * MockScanExecutor is a memory-buffered scan operator used primarily for unit testing
 * and for injecting synthetic or foreign relations (such as OrderRow) into execution pipelines.
 */
template <typename RowType>
class MockScanExecutor : public AbstractExecutor<RowType> {
public:
    explicit MockScanExecutor(std::vector<RowType> rows)
        : rows_(std::move(rows)), cursor_(0) {}

    void Init() override {
        cursor_ = 0;
    }

    bool Next(RowType *out_row, RID *out_rid) override {
        if (cursor_ >= rows_.size()) {
            return false;
        }
        if (out_row != nullptr) {
            *out_row = rows_[cursor_];
        }
        if (out_rid != nullptr) {
            *out_rid = RID(999, static_cast<uint32_t>(cursor_));
        }
        cursor_++;
        return true;
    }

    inline size_t Size() const { return rows_.size(); }

private:
    std::vector<RowType> rows_;
    size_t cursor_{0};
};
