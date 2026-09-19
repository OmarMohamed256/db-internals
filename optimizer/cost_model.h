#pragma once

#include <cmath>
#include <cstddef>
#include "statistics.h"

/**
 * CostModel: Mathematical hardware cost formulations based on System R / Selinger model
 * and CMU 15-445 Lecture #15 & #16.
 */
class CostModel {
public:
    // Physical cost weights (normalized to sequential 4KB page read = 1.0)
    static constexpr double PAGE_READ_COST_SEQ = 1.0;     // Sequential block read
    static constexpr double PAGE_READ_COST_RANDOM = 4.0;  // Random block seek / buffer miss
    static constexpr double TUPLE_CPU_COST = 0.01;        // CPU tuple evaluation
    static constexpr double JOIN_CPU_COST = 0.02;         // CPU join comparison
    static constexpr double HASH_BUILD_COST = 0.03;       // CPU hash compute & table insert
    static constexpr double HASH_PROBE_COST = 0.02;       // CPU hash probe

    /**
     * Compute cost of a Sequential Table Scan over TableHeap.
     * Pure sequential streaming reading P pages, then evaluating N tuples.
     * Formula: P * Cost_seq + N * Cost_cpu
     */
    static double CostSeqScan(const TableStats &stats) {
        return (static_cast<double>(stats.GetNumPages()) * PAGE_READ_COST_SEQ) +
               (static_cast<double>(stats.GetNumTuples()) * TUPLE_CPU_COST);
    }

    /**
     * Compute cost of an Index Scan over B+ Tree primary index.
     * 1. Traverses internal nodes (height h).
     * 2. Reads leaf pages in range: ceil(sel * P_leaf).
     * 3. Random I/O tuple dereferences into TableHeap: (sel * N) * Cost_random.
     * 4. CPU tuple inspection: (sel * N) * Cost_cpu.
     */
    static double CostIndexScan(const TableStats &stats, double selectivity,
                               int tree_height = 2, size_t index_leaf_pages = 10) {
        double matching_tuples = selectivity * static_cast<double>(stats.GetNumTuples());
        double leaf_pages_read = std::ceil(selectivity * static_cast<double>(index_leaf_pages));

        double index_io_cost = (static_cast<double>(tree_height) + leaf_pages_read) * PAGE_READ_COST_SEQ;
        double heap_deref_cost = matching_tuples * PAGE_READ_COST_RANDOM;
        double cpu_cost = matching_tuples * TUPLE_CPU_COST;

        return index_io_cost + heap_deref_cost + cpu_cost;
    }

    /**
     * Compute cost of a Nested Loop Join.
     * Formula: Cost(Outer) + (|Outer| * Cost(Inner)) + (|Outer| * |Inner| * Cost_join_cpu)
     */
    static double CostNestedLoopJoin(double cost_outer, size_t card_outer,
                                     double cost_inner, size_t card_inner) {
        double io_cost = cost_outer + (static_cast<double>(card_outer) * cost_inner);
        double cpu_cost = static_cast<double>(card_outer) * static_cast<double>(card_inner) * JOIN_CPU_COST;
        return io_cost + cpu_cost;
    }

    /**
     * Compute cost of an In-Memory Hash Join.
     * Formula: Cost(Outer) + Cost(Inner) +
     *          (|Outer| * Cost_hash_build) + (|Inner| * Cost_hash_probe) +
     *          (|Joined| * Cost_tuple_cpu)
     */
    static double CostHashJoin(double cost_outer, size_t card_outer,
                               double cost_inner, size_t card_inner,
                               size_t card_joined) {
        double io_cost = cost_outer + cost_inner;
        double cpu_build_cost = static_cast<double>(card_outer) * HASH_BUILD_COST;
        double cpu_probe_cost = static_cast<double>(card_inner) * HASH_PROBE_COST;
        double cpu_emit_cost = static_cast<double>(card_joined) * TUPLE_CPU_COST;
        return io_cost + cpu_build_cost + cpu_probe_cost + cpu_emit_cost;
    }
};
