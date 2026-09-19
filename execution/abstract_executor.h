#pragma once

#include "../storage/rid.h"

/**
 * AbstractExecutor is the pure virtual base class for all Volcano-style iterators
 * (CMU 15-445 Lecture #13: Query Execution).
 *
 * In the Volcano (or Iterator / Pipeline) processing model:
 * - Query plans are organized as a tree of operators.
 * - Tuples are pulled from bottom to top: parent operators call child->Next().
 * - Each operator implements two core methods:
 *     1. Init(): Resets the operator's internal state and positions cursors at the start.
 *        Called multiple times on inner loops (e.g. in NestedLoopJoin).
 *     2. Next(out_row, out_rid): Emits the next available tuple and returns true,
 *        or returns false when the stream reaches EOF.
 *
 * This design enables true streaming pipelining with O(1) memory overhead for
 * streaming unary operators (Filter, Limit, Projection).
 */
template <typename RowType>
class AbstractExecutor {
public:
    virtual ~AbstractExecutor() = default;

    /**
     * Initializes or resets the operator state.
     * Must be called before the first Next() invocation.
     */
    virtual void Init() = 0;

    /**
     * Yields the next tuple from this operator.
     * @param[out] out_row Destination pointer to store the yielded record.
     * @param[out] out_rid Optional destination pointer to store the tuple's RID (can be nullptr).
     * @return true if a valid tuple was yielded, false if stream reached EOF.
     */
    virtual bool Next(RowType *out_row, RID *out_rid) = 0;
};
