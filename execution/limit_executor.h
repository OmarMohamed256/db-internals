#pragma once

#include <cstddef>
#include "abstract_executor.h"

/**
 * LimitExecutor limits the maximum number of tuples yielded by its child executor.
 * Intermediate Volcano operator (Unary streaming pipeline operator).
 *
 * Pipelined / Streaming: Immediately returns false once `limit_` tuples have been emitted.
 */
template <typename RowType>
class LimitExecutor : public AbstractExecutor<RowType> {
public:
    LimitExecutor(AbstractExecutor<RowType> *child, size_t limit)
        : child_(child), limit_(limit), current_count_(0) {}

    void Init() override {
        // TODO(student):
        // 1. Reset current_count_ = 0.
        // 2. If child_ != nullptr, call child_->Init().
        current_count_ = 0;
        if(child_ != nullptr) child_->Init();
    }

    bool Next(RowType *out_row, RID *out_rid) override {
        // TODO(student):
        // 1. If current_count_ >= limit_, return false (limit reached).
        // 2. Fetch the next tuple from child_->Next(out_row, out_rid).
        //    If child returns false, return false (child exhausted).
        // 3. Increment current_count_++.
        // 4. Return true.
        if(current_count_ >= limit_) return false;
        if(!child_->Next(out_row, out_rid)) return false;
        current_count_++;
        return true;
    }

    inline size_t GetEmittedCount() const { return current_count_; }

private:
    AbstractExecutor<RowType> *child_{nullptr};
    size_t limit_{0};
    size_t current_count_{0};
};
