#pragma once

#include "../storage/user_row.h"
#include "abstract_executor.h"
#include "order_row.h"

/**
 * NestedLoopJoinExecutor implements a simple nested loop equi-join between
 * an outer table (UserRow) and an inner table (OrderRow).
 *
 * Join Condition: users.id == orders.user_id
 * Output Schema: JoinedUserOrderRow
 *
 * Execution Pattern:
 * For each outer row:
 *   For each inner row:
 *     If outer.id == inner.user_id:
 *       Emit JoinedUserOrderRow(outer, inner)
 *
 * Algorithmic Complexity:
 * - Time: O(|Outer| * |Inner|) Next() invocations (Quadratic).
 * - Space: O(1) auxiliary memory (streaming).
 */
class NestedLoopJoinExecutor : public AbstractExecutor<JoinedUserOrderRow> {
public:
  NestedLoopJoinExecutor(AbstractExecutor<UserRow> *outer_child,
                         AbstractExecutor<OrderRow> *inner_child)
      : outer_child_(outer_child), inner_child_(inner_child) {}

  void Init() override {
    // TODO(student):
    // 1. Initialize outer_child_->Init().
    // 2. Initialize inner_child_->Init().
    // 3. Fetch the first outer row:
    //    has_outer_ = outer_child_->Next(&curr_outer_row_, &curr_outer_rid_);
    outer_child_->Init();
    inner_child_->Init();
    has_outer_ = outer_child_->Next(&curr_outer_row_, &curr_outer_rid_);
  }

  bool Next(JoinedUserOrderRow *out_row, RID *out_rid) override {
    // TODO(student):
    // 1. While has_outer_ is true:
    //    a. While inner_child_->Next(&curr_inner_row_, &curr_inner_rid_):
    //       - Check join predicate: curr_outer_row_.id ==
    //       curr_inner_row_.user_id
    //       - If match found:
    //           *out_row = JoinedUserOrderRow(curr_outer_row_,
    //           curr_inner_row_); if (out_rid != nullptr) *out_rid =
    //           curr_outer_rid_; return true;
    //    b. Inner child is exhausted for the current outer row!
    //       - Rewind inner child: inner_child_->Init();
    //       - Fetch next outer row:
    //         has_outer_ = outer_child_->Next(&curr_outer_row_,
    //         &curr_outer_rid_);
    // 2. When outer child is exhausted, return false.
    while (has_outer_) {
      while (inner_child_->Next(&curr_inner_row_, &curr_inner_rid_)) {
        if (curr_outer_row_.id == curr_inner_row_.user_id) {
            // matching tuple
            *out_row = JoinedUserOrderRow(curr_outer_row_, curr_inner_row_);
            if(out_rid != nullptr)
                *out_rid = curr_outer_rid_;
            return true;
        }
      }
      // inner exhausted
      inner_child_->Init();
      has_outer_ = outer_child_->Next(&curr_outer_row_, &curr_outer_rid_);
      while(!has_outer_){
          return false;
      }
    }
    return false;
  }

private:
  AbstractExecutor<UserRow> *outer_child_{nullptr};
  AbstractExecutor<OrderRow> *inner_child_{nullptr};

  UserRow curr_outer_row_{};
  RID curr_outer_rid_{};
  OrderRow curr_inner_row_{};
  RID curr_inner_rid_{};
  bool has_outer_{false};
};
