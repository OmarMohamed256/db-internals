#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "abstract_plan_node.h"
#include "cost_model.h"
#include "statistics.h"

/**
 * JoinEdge: Represents an equi-join condition between two tables.
 * Example: table1.col1 = table2.col2
 */
struct JoinEdge {
  std::string left_table;
  std::string left_col;
  std::string right_table;
  std::string right_col;

  JoinEdge() = default;
  JoinEdge(std::string lt, std::string lc, std::string rt, std::string rc)
      : left_table(std::move(lt)), left_col(std::move(lc)),
        right_table(std::move(rt)), right_col(std::move(rc)) {}

  std::string ToString() const {
    return left_table + "." + left_col + " = " + right_table + "." + right_col;
  }
};

/**
 * TableIndexInfo: Metadata about available B+ Tree indexes on a table.
 */
struct TableIndexInfo {
  std::string index_name;
  std::string indexed_column;
  int tree_height{2};
  size_t leaf_pages{10};
};

/**
 * SelingerOptimizer: Cost-Based Optimizer implementing:
 * 1. Single-Table Access Path Selection (SeqScan vs IndexScan based on
 * selectivity).
 * 2. Rule-Based Filter Pushdown (pushing filters below joins).
 * 3. Selinger Dynamic Programming (DP) Join Order Enumeration with bitmask
 * states.
 */
class SelingerOptimizer {
public:
  SelingerOptimizer() = default;

  void RegisterTable(const TableStats &stats) {
    catalog_[stats.GetTableName()] = stats;
  }

  void RegisterIndex(const std::string &table_name,
                     const TableIndexInfo &idx_info) {
    indexes_[table_name].push_back(idx_info);
  }

  /**
   * Choose the optimal physical access path for a single table given a range
   * filter. Compares Cost(SeqScan) vs Cost(IndexScan) and returns the cheaper
   * plan node.
   */
  std::shared_ptr<AbstractPlanNode>
  SelectAccessPath(const std::string &table_name, const std::string &filter_col,
                   double low_key, double high_key) {
    TableStats stats = catalog_[table_name];
    double cost_seq = CostModel::CostSeqScan(stats);

    // 1. Search for an index on filter_col
    const TableIndexInfo *matching_idx = nullptr;
    auto it = indexes_.find(table_name);
    if (it != indexes_.end()) {
      for (const auto &idx : it->second) {
        if (idx.indexed_column == filter_col) {
          matching_idx = &idx;
          break;
        }
      }
    }

    // 2. If an index exists on this column, evaluate its cost
    if (matching_idx != nullptr) {
      double sel =
          (low_key == high_key)
              ? stats.EstimateEqualitySelectivity(filter_col)
              : stats.EstimateRangeSelectivity(filter_col, low_key, high_key);
      double cost_idx = CostModel::CostIndexScan(
          stats, sel, matching_idx->tree_height, matching_idx->leaf_pages);

      if (cost_idx < cost_seq) {
        size_t matched_rows =
            static_cast<size_t>(std::ceil(sel * stats.GetNumTuples()));
        return std::make_shared<IndexScanPlanNode>(
            table_name, matching_idx->index_name, low_key, high_key, cost_idx,
            matched_rows);
      }
    }

    return std::make_shared<SeqScanPlanNode>(table_name, cost_seq,
                                             stats.GetNumTuples());
  }
  /**
   * Rule-Based Optimization: Pushdown Filter below Join.
   * If a filter only references columns belonging to the left or right child of
   * a join, push the filter node directly on top of that child.
   */
  std::shared_ptr<AbstractPlanNode>
  PushdownFilter(const std::shared_ptr<FilterPlanNode> &filter_node) {
    if (!filter_node)
      return nullptr;
    auto child = filter_node->GetChild();
    if (!child || (child->GetType() != PlanNodeType::HASH_JOIN &&
                   child->GetType() != PlanNodeType::NESTED_LOOP_JOIN)) {
      return filter_node;
    }

    auto join = std::dynamic_pointer_cast<JoinPlanNode>(child);
    std::string left_table, right_table;
    if (auto scan =
            std::dynamic_pointer_cast<SeqScanPlanNode>(join->GetLeft())) {
      left_table = scan->GetTableName();
    } else if (auto idx = std::dynamic_pointer_cast<IndexScanPlanNode>(
                   join->GetLeft())) {
      left_table = idx->GetTableName();
    }

    if (auto scan =
            std::dynamic_pointer_cast<SeqScanPlanNode>(join->GetRight())) {
      right_table = scan->GetTableName();
    } else if (auto idx = std::dynamic_pointer_cast<IndexScanPlanNode>(
                   join->GetRight())) {
      right_table = idx->GetTableName();
    }

    const std::string &pred = filter_node->GetPredicate();
    bool refs_left = (!left_table.empty() &&
                      pred.find(left_table + ".") != std::string::npos);
    bool refs_right = (!right_table.empty() &&
                       pred.find(right_table + ".") != std::string::npos);
    if (refs_left && refs_right) {
      return filter_node;
    }

    if (refs_left) {
      auto new_left = std::make_shared<FilterPlanNode>(
          join->GetLeft(), filter_node->GetPredicate(), filter_node->GetCost(),
          filter_node->GetEstimatedCardinality());
      return std::make_shared<JoinPlanNode>(
          join->GetType(), new_left, join->GetRight(), join->GetJoinKey(),
          join->GetCost(), join->GetEstimatedCardinality());
    }

    if (refs_right) {
      auto new_right = std::make_shared<FilterPlanNode>(
          join->GetRight(), filter_node->GetPredicate(), filter_node->GetCost(),
          filter_node->GetEstimatedCardinality());
      return std::make_shared<JoinPlanNode>(
          join->GetType(), join->GetLeft(), new_right, join->GetJoinKey(),
          join->GetCost(), join->GetEstimatedCardinality());
    }

    return filter_node;
  }

  /**
   * Selinger Dynamic Programming (DP) Join Ordering:
   * Finds the optimal join order and physical join operators (HashJoin vs
   * NestedLoopJoin) for N tables using bitmask state representation.
   *
   * State: bitmask S where bit i is 1 if tables[i] is included in the join
   * subset. Recurrence: OPT[S] = min_{S1 subset S, S2 = S \ S1} (OPT[S1] Join
   * OPT[S2])
   */
  std::shared_ptr<AbstractPlanNode>
  OptimizeJoinOrderDP(const std::vector<std::string> &tables,
                      const std::vector<JoinEdge> &edges) {
    const size_t N = tables.size();
    memo_.clear();

    // ========================================================================
    // PHASE 1: Base Relations (Subsets of Size 1)
    // ------------------------------------------------------------------------
    // For each table i, assign bitmask (1 << i) and store its optimal access path.
    // In this milestone, base tables have no range filters, so each uses SeqScan.
    //
    // TRACE - LEVEL 1 (Base Relations):
    //   Table 0 ("users"):      mask = (1 << 0) = 1 (binary 001)
    //     -> memo_[1] = SeqScan(users)      [Cost: 6.00,    Rows: 100]
    //   Table 1 ("orders"):     mask = (1 << 1) = 2 (binary 010)
    //     -> memo_[2] = SeqScan(orders)     [Cost: 150.00,  Rows: 5000]
    //   Table 2 ("line_items"): mask = (1 << 2) = 4 (binary 100)
    //     -> memo_[4] = SeqScan(line_items) [Cost: 1500.00, Rows: 50000]
    // ========================================================================
    for (size_t i = 0; i < N; ++i) {
      uint32_t mask = (1U << i);
      TableStats stats = catalog_[tables[i]];
      double cost_seq = CostModel::CostSeqScan(stats);
      memo_[mask] = std::make_shared<SeqScanPlanNode>(tables[i], cost_seq,
                                                      stats.GetNumTuples());
    }

    // ========================================================================
    // PHASE 2: Bottom-Up Subset Search (Subsets of Size 2 to N)
    // ------------------------------------------------------------------------
    // Progressively enumerate subproblems by size: pairs (2), triplets (3), etc.
    // For each subset S, evaluate all left-deep partitions (S1 = S ^ s2, S2 = s2).
    //
    // TRACE - LEVEL 2 (2-Table Pairs, e.g. S = 3 -> binary 011 = {users, orders}):
    //   Partition with j = 1 ("orders"):
    //     s2 = (1 << 1) = 2 ("orders"), s1 = 3 ^ 2 = 1 ("users")
    //     plan_left  = memo_[1] (Cost: 6.00,   Rows: 100)
    //     plan_right = memo_[2] (Cost: 150.00, Rows: 5000)
    //     edge       = "users.id = orders.user_id", joined_card = 5000
    //     cost_hj    = 6 + 150 + (100*0.03) + (5000*0.02) + (5000*0.01) = 309.00
    //     -> memo_[3] = HashJoin(users, orders) [Cost: 309.00]
    //
    //   Tree for memo_[3]:
    //              HashJoin (users.id = orders.user_id) [Cost: 309.00]
    //             /                                    \ [right child]
    //      memo_[1]: SeqScan(users)             memo_[2]: SeqScan(orders)
    //      [Cost: 6.00, Rows: 100]              [Cost: 150.00, Rows: 5000]
    //
    // TRACE - LEVEL 3 (3-Table Full Join, S = 7 -> binary 111 = {users, orders, line_items}):
    //   Partition with j = 2 ("line_items"):
    //     s2 = (1 << 2) = 4 ("line_items"), s1 = 7 ^ 4 = 3 ({users, orders})
    //     plan_left  = memo_[3] (Cost: 309.00,  Rows: 5000)   <-- Reused from Level 2!
    //     plan_right = memo_[4] (Cost: 1500.00, Rows: 50000)
    //     edge       = "orders.id = line_items.order_id", joined_card = 50000
    //     cost_hj    = 309 + 1500 + (5000*0.03) + (50000*0.02) + (50000*0.01) = 3459.00
    //     -> memo_[7] = HashJoin(memo_[3], memo_[4]) [Cost: 3459.00]
    //
    //   Tree for memo_[7] (Left-Deep structure):
    //                     HashJoin (orders.id = line_items.order_id) [Cost: 3459.00]
    //                    /                                          \ [right table]
    //            memo_[3] (from Level 2)                     memo_[4]: SeqScan(line_items)
    //            HashJoin (users.id = orders.user_id)        [Cost: 1500.00, Rows: 50000]
    //           /                                    \ [right table]
    //   memo_[1]: SeqScan(users)             memo_[2]: SeqScan(orders)
    //   [Cost: 6.00, Rows: 100]              [Cost: 150.00, Rows: 5000]
    // ========================================================================
    for (size_t size = 2; size <= N; ++size) {
      for (uint32_t S = 1; S < (1U << N); ++S) {
        if (static_cast<size_t>(__builtin_popcount(S)) != size) {
          continue;
        }

        double best_cost = std::numeric_limits<double>::infinity();
        std::shared_ptr<AbstractPlanNode> best_plan = nullptr;

        // Partition S into non-empty S1 (left subtree) and S2 (single table j):
        // System R left-deep enumeration: right child S2 is always a single table.
        for (size_t j = 0; j < N; ++j) {
          if ((S & (1U << j)) == 0) {
            continue; // Table j is not part of subset S
          }

          uint32_t s2 = (1U << j); // Right child: single table j
          uint32_t s1 = S ^ s2;    // Left child: subset S without table j

          if (memo_.find(s1) == memo_.end() || memo_.find(s2) == memo_.end()) {
            continue;
          }

          auto plan_left = memo_[s1];
          auto plan_right = memo_[s2];

          // Search query edges to find join condition connecting s1 and s2:
          JoinEdge matched_edge;
          bool has_edge = false;
          for (const auto &edge : edges) {
            for (size_t k = 0; k < N; ++k) {
              if ((s1 & (1U << k)) == 0) continue;
              if ((edge.left_table == tables[k] && edge.right_table == tables[j]) ||
                  (edge.left_table == tables[j] && edge.right_table == tables[k])) {
                matched_edge = edge;
                has_edge = true;
                break;
              }
            }
            if (has_edge) break;
          }

          // If an edge exists, use its predicate and equi-join cardinality.
          // If no edge exists, this is a Cartesian/Cross Product (penalized with card_left * card_right).
          std::string join_key = has_edge ? matched_edge.ToString() : "CROSS PRODUCT";
          size_t joined_card = has_edge
              ? std::max(plan_left->GetEstimatedCardinality(), plan_right->GetEstimatedCardinality())
              : (plan_left->GetEstimatedCardinality() * plan_right->GetEstimatedCardinality());

          // Evaluate Hash Join for this join ordering partition:
          double cost_hj = CostModel::CostHashJoin(
              plan_left->GetCost(), plan_left->GetEstimatedCardinality(),
              plan_right->GetCost(), plan_right->GetEstimatedCardinality(),
              joined_card);
          if (cost_hj < best_cost) {
            best_cost = cost_hj;
            best_plan = std::make_shared<JoinPlanNode>(
                PlanNodeType::HASH_JOIN, plan_left, plan_right,
                join_key, cost_hj, joined_card);
          }
        }

        if (best_plan != nullptr) {
          memo_[S] = best_plan;
        }
      }
    }

    // ========================================================================
    // PHASE 3: Return the Global Optimal Plan
    // ------------------------------------------------------------------------
    // Mask (1 << N) - 1 has all N bits set to 1, representing the full query.
    //
    // Final Optimal Left-Deep Plan Tree:
    //
    //   HashJoin(ON orders.id = line_items.order_id) [Cost: 3459.00, Rows: 50000]
    //   ├── Left:  HashJoin(ON users.id = orders.user_id) [Cost: 309.00, Rows: 5000]
    //   │          ├── Left:  SeqScan(users) [Cost: 6.00, Rows: 100]
    //   │          └── Right: SeqScan(orders) [Cost: 150.00, Rows: 5000]
    //   └── Right: SeqScan(line_items) [Cost: 1500.00, Rows: 50000]
    // ========================================================================
    return memo_[(1U << N) - 1];
  }

  inline const std::unordered_map<uint32_t, std::shared_ptr<AbstractPlanNode>> &
  GetMemoTable() const {
    return memo_;
  }

private:
  std::unordered_map<std::string, TableStats> catalog_;
  std::unordered_map<std::string, std::vector<TableIndexInfo>> indexes_;
  std::unordered_map<uint32_t, std::shared_ptr<AbstractPlanNode>> memo_;
};
