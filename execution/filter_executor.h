#pragma once

#include <functional>
#include <utility>
#include "abstract_executor.h"

/**
 * FilterExecutor evaluates an arbitrary predicate lambda over rows pulled from a child executor.
 * Intermediate Volcano operator (Unary streaming pipeline operator).
 *
 * Pipelined / Streaming: Does NOT buffer rows in memory. Space complexity is O(1).
 */
template <typename RowType>
class FilterExecutor : public AbstractExecutor<RowType> {
public:
    using PredicateFn = std::function<bool(const RowType &)>;

    FilterExecutor(AbstractExecutor<RowType> *child, PredicateFn predicate)
        : child_(child), predicate_(std::move(predicate)) {}

    void Init() override {
        if(child_ != nullptr){
            child_->Init();
        }
    }

    bool Next(RowType *out_row, RID *out_rid) override {
        while (child_->Next(out_row, out_rid)) {
            if(predicate_(*out_row))
                return true;
        }
        return false;
    }

private:
    AbstractExecutor<RowType> *child_{nullptr};
    PredicateFn predicate_;
};
