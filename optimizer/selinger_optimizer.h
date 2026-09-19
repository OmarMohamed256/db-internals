#pragma once

#include <chrono>
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
          stats.EstimateRangeSelectivity(filter_col, low_key, high_key);
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
    // TODO(student):
    // 1. If filter child is not a join, return filter_node unmodified.
    // 2. If child is a JoinPlanNode:
    //    a. Inspect predicate to determine if it refers strictly to left child
    //    table or right child table. b. If target is left child:
    //         new_left = make_shared<FilterPlanNode>(join->GetLeft(),
    //         filter->GetPredicate(), ...); return
    //         make_shared<JoinPlanNode>(join->GetType(), new_left,
    //         join->GetRight(), ...);
    //    c. If target is right child:
    //         new_right = make_shared<FilterPlanNode>(join->GetRight(),
    //         filter->GetPredicate(), ...); return
    //         make_shared<JoinPlanNode>(join->GetType(), join->GetLeft(),
    //         new_right, ...);
    // 3. If predicate references both tables, filter cannot be pushed down;
    // return filter_node.
    (void)filter_node;
    return nullptr;
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
    // TODO(student):
    // const size_t N = tables.size();
    // memo_.clear();
    //
    // 1. SIZE 1 (Base relations):
    //    For each table i in [0, N - 1]:
    //      uint32_t mask = (1 << i);
    //      memo_[mask] = default scan plan for tables[i] (via SelectAccessPath
    //      or SeqScanPlanNode).
    //
    // 2. SIZE 2 to N (Bottom-up subset search):
    //    For subset_size = 2 to N:
    //      For each subset mask S in [1, (1 << N) - 1] where
    //      __builtin_popcount(S) == subset_size:
    //        double best_cost = std::numeric_limits<double>::infinity();
    //        std::shared_ptr<AbstractPlanNode> best_plan = nullptr;
    //
    //        // Partition S into non-empty S1 and S2 (left-deep or bushy):
    //        For each table j where ((1 << j) & S) != 0:
    //          uint32_t s2 = (1 << j);
    //          uint32_t s1 = S ^ s2;
    //          if (memo_.find(s1) == memo_.end() || memo_.find(s2) ==
    //          memo_.end()) continue;
    //
    //          auto plan_left = memo_[s1];
    //          auto plan_right = memo_[s2];
    //
    //          // Check join edge connecting s1 and s2:
    //          JoinEdge edge = FindEdge(s1, s2, tables, edges);
    //          size_t joined_card = EstimateJoinCardinality(plan_left,
    //          plan_right, edge);
    //
    //          // Evaluate Hash Join:
    //          double cost_hj = CostModel::CostHashJoin(plan_left->GetCost(),
    //          plan_left->GetEstimatedCardinality(),
    //                                                   plan_right->GetCost(),
    //                                                   plan_right->GetEstimatedCardinality(),
    //                                                   joined_card);
    //          if (cost_hj < best_cost) {
    //              best_cost = cost_hj;
    //              best_plan =
    //              std::make_shared<JoinPlanNode>(PlanNodeType::HASH_JOIN,
    //              plan_left, plan_right,
    //                                                         edge.ToString(),
    //                                                         cost_hj,
    //                                                         joined_card);
    //          }
    //
    //          // Evaluate Nested Loop Join:
    //          double cost_nlj =
    //          CostModel::CostNestedLoopJoin(plan_left->GetCost(),
    //          plan_left->GetEstimatedCardinality(),
    //                                                          plan_right->GetCost(),
    //                                                          plan_right->GetEstimatedCardinality());
    //          if (cost_nlj < best_cost) {
    //              best_cost = cost_nlj;
    //              best_plan =
    //              std::make_shared<JoinPlanNode>(PlanNodeType::NESTED_LOOP_JOIN,
    //              plan_left, plan_right,
    //                                                         edge.ToString(),
    //                                                         cost_nlj,
    //                                                         joined_card);
    //          }
    //
    //        memo_[S] = best_plan;
    //
    // 3. Return the plan corresponding to all tables: memo_[(1 << N) - 1].
    (void)tables;
    (void)edges;
    return nullptr;
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
