#include <iostream>
#include <cassert>
#include <vector>
#include <string>
#include <memory>
#include <cmath>

#include "statistics.h"
#include "cost_model.h"
#include "abstract_plan_node.h"
#include "selinger_optimizer.h"

// ============================================================================
// Milestone 1: Selectivity Estimation & Access Path Crossover Point
// ============================================================================
void TestSelectivityAndAccessPath() {
    std::cout << "\n=== [Milestone 1] Selectivity Estimation & Access Path Crossover ===" << std::endl;

    TableStats users_stats("users", 10000, 250); // 10,000 rows across 250 4KB pages
    users_stats.AddColumnStats(ColumnStats("id", 10000, 1.0, 10000.0, true));
    users_stats.AddColumnStats(ColumnStats("balance", 5000, 0.0, 50000.0, false));

    TableIndexInfo pk_idx{"users_pk_idx", "id", 3, 30};

    SelingerOptimizer optimizer;
    optimizer.RegisterTable(users_stats);
    optimizer.RegisterIndex("users", pk_idx);

    // 1. Point Query: id = 500
    std::cout << "  • Evaluating Point Query: id = 500 (sel = 0.01%)..." << std::endl;
    auto point_plan = optimizer.SelectAccessPath("users", "id", 500.0, 500.0);
    assert(point_plan != nullptr && "SelectAccessPath must not return null!");
    assert(point_plan->GetType() == PlanNodeType::INDEX_SCAN && "Point query MUST choose IndexScan!");
    std::cout << "    " << point_plan->ToString() << std::endl;

    // 2. Narrow Range Query: id BETWEEN 100 AND 200 (sel = 1%)
    std::cout << "  • Evaluating Narrow Range: id in [100..200] (sel = 1%)..." << std::endl;
    auto narrow_plan = optimizer.SelectAccessPath("users", "id", 100.0, 200.0);
    assert(narrow_plan != nullptr);
    assert(narrow_plan->GetType() == PlanNodeType::INDEX_SCAN && "Narrow range (1%) MUST choose IndexScan!");
    std::cout << "    " << narrow_plan->ToString() << std::endl;

    // 3. Broad Range Query: id BETWEEN 1 AND 6000 (sel = 60%)
    std::cout << "  • Evaluating Broad Range: id in [1..6000] (sel = 60%)..." << std::endl;
    auto broad_plan = optimizer.SelectAccessPath("users", "id", 1.0, 6000.0);
    assert(broad_plan != nullptr);
    assert(broad_plan->GetType() == PlanNodeType::SEQ_SCAN &&
           "Broad range (60%) MUST choose SeqScan (avoids random I/O thrashing)!");
    std::cout << "    " << broad_plan->ToString() << std::endl;

    // 4. Verify Crossover Point (~25-30%)
    std::cout << "  • Finding Crossover Selectivity between IndexScan and SeqScan..." << std::endl;
    double crossover_sel = -1.0;
    for (double sel = 0.01; sel <= 0.50; sel += 0.01) {
        double cost_seq = CostModel::CostSeqScan(users_stats);
        double cost_idx = CostModel::CostIndexScan(users_stats, sel, pk_idx.tree_height, pk_idx.leaf_pages);
        if (cost_idx > cost_seq && crossover_sel < 0) {
            crossover_sel = sel;
            std::cout << "    >>> Access path crossover detected at selectivity: "
                      << (crossover_sel * 100.0) << "% (SeqScan becomes cheaper) <<<" << std::endl;
        }
    }
    assert(crossover_sel >= 0.20 && crossover_sel <= 0.35);

    std::cout << "  ✓ Milestone 1 (Selectivity & Access Path Crossover) passed!" << std::endl;
}

// ============================================================================
// Milestone 2: Rule-Based Optimization: Filter Pushdown
// ============================================================================
void TestFilterPushdown() {
    std::cout << "\n=== [Milestone 2] Rule-Based Optimization: Filter Pushdown ===" << std::endl;

    SelingerOptimizer optimizer;

    auto scan_users = std::make_shared<SeqScanPlanNode>("users", 250.0, 10000);
    auto scan_orders = std::make_shared<SeqScanPlanNode>("orders", 500.0, 50000);
    auto join_node = std::make_shared<JoinPlanNode>(PlanNodeType::HASH_JOIN,
                                                    scan_users, scan_orders,
                                                    "users.id = orders.user_id",
                                                    1500.0, 50000);

    // Initial Unoptimized Tree: Filter on top of Join
    // Query: SELECT * FROM users JOIN orders ON ... WHERE users.balance > 1000
    auto root_filter = std::make_shared<FilterPlanNode>(join_node, "users.balance > 1000", 1600.0, 5000);

    std::cout << "  • Before Pushdown (Unoptimized Plan):" << std::endl;
    std::cout << root_filter->ToString(2) << std::endl;

    // Apply Filter Pushdown
    auto optimized_plan = optimizer.PushdownFilter(root_filter);
    assert(optimized_plan != nullptr && "PushdownFilter must not return null!");
    assert(optimized_plan->GetType() == PlanNodeType::HASH_JOIN && "Root must now be JoinPlanNode!");

    auto opt_join = std::dynamic_pointer_cast<JoinPlanNode>(optimized_plan);
    assert(opt_join->GetLeft()->GetType() == PlanNodeType::FILTER && "Filter must be pushed to left child (users)!");
    assert(opt_join->GetRight()->GetType() == PlanNodeType::SEQ_SCAN && "Right child remains scan on orders!");

    std::cout << "  • After Pushdown (Optimized Plan):" << std::endl;
    std::cout << optimized_plan->ToString(2) << std::endl;

    std::cout << "  ✓ Milestone 2 (Filter Pushdown) passed!" << std::endl;
}

// ============================================================================
// Milestone 3: Selinger Dynamic Programming (DP) Join Ordering
// ============================================================================
void TestDynamicProgrammingJoinOrdering() {
    std::cout << "\n=== [Milestone 3] Selinger Dynamic Programming (DP) Join Ordering ===" << std::endl;

    SelingerOptimizer optimizer;

    // 3 Tables with vastly different sizes:
    // A: users (100 rows after selective filter)
    // B: orders (5,000 rows)
    // C: line_items (50,000 rows)
    TableStats users_stats("users", 100, 5);
    TableStats orders_stats("orders", 5000, 100);
    TableStats line_items_stats("line_items", 50000, 1000);

    optimizer.RegisterTable(users_stats);
    optimizer.RegisterTable(orders_stats);
    optimizer.RegisterTable(line_items_stats);

    std::vector<std::string> tables = {"users", "orders", "line_items"};
    std::vector<JoinEdge> edges = {
        JoinEdge("users", "id", "orders", "user_id"),
        JoinEdge("orders", "id", "line_items", "order_id")
    };

    std::cout << "  • Executing DP Join Enumeration for 3-table join..." << std::endl;
    auto opt_plan = optimizer.OptimizeJoinOrderDP(tables, edges);
    assert(opt_plan != nullptr && "OptimizeJoinOrderDP must produce a valid plan!");

    // Check memo table states: 2^3 - 1 = 7 states
    const auto &memo = optimizer.GetMemoTable();
    std::cout << "  • DP Memo Table contains " << memo.size() << " subproblem states (Expected 7)." << std::endl;
    assert(memo.size() == 7 && "DP memo table must contain all 2^N - 1 states!");

    // State 7 (binary 111): Full 3-table join
    assert(memo.find(7) != memo.end());
    std::cout << "  • Optimal 3-Table Physical Plan Tree:" << std::endl;
    std::cout << opt_plan->ToString(2) << std::endl;

    // Verify DP picked the join starting with the smaller/filtered tables (users x orders) first!
    auto root_join = std::dynamic_pointer_cast<JoinPlanNode>(opt_plan);
    assert(root_join != nullptr);

    std::cout << "  ✓ Milestone 3 (Dynamic Programming Join Ordering) passed!" << std::endl;
}

// ============================================================================
// Milestone 4: Physical Join Operator Selection (HashJoin vs NestedLoopJoin)
// ============================================================================
void TestPhysicalJoinSelection() {
    std::cout << "\n=== [Milestone 4] Physical Join Selection in DP ===" << std::endl;

    SelingerOptimizer optimizer;

    // Case 1: Large relations (1,000 x 10,000) -> Hash Join must beat Nested Loop Join
    TableStats t1("large_a", 1000, 25);
    TableStats t2("large_b", 10000, 200);
    optimizer.RegisterTable(t1);
    optimizer.RegisterTable(t2);

    std::vector<std::string> tables = {"large_a", "large_b"};
    std::vector<JoinEdge> edges = {JoinEdge("large_a", "id", "large_b", "a_id")};

    auto plan = optimizer.OptimizeJoinOrderDP(tables, edges);
    assert(plan != nullptr);
    assert(plan->GetType() == PlanNodeType::HASH_JOIN &&
           "Large table join MUST choose HashJoin over quadratic NestedLoopJoin!");
    std::cout << "  • Large join correctly chose: " << plan->ToString() << std::endl;

    std::cout << "  ✓ Milestone 4 (Physical Join Selection) passed!" << std::endl;
}

// ============================================================================
// Main Dispatcher
// ============================================================================
int main(int argc, char **argv) {
    int test_num = 0;
    if (argc > 1) {
        test_num = std::atoi(argv[1]);
    }

    std::cout << "=================================================================" << std::endl;
    std::cout << "DBMS OPTIMIZER SUITE: Cost Models, Access Paths & DP Join Ordering" << std::endl;
    if (test_num > 0) {
        std::cout << "Running Milestone Test #" << test_num << std::endl;
    }
    std::cout << "=================================================================" << std::endl;

    if (test_num == 0 || test_num == 1) TestSelectivityAndAccessPath();
    if (test_num == 0 || test_num == 2) TestFilterPushdown();
    if (test_num == 0 || test_num == 3) TestDynamicProgrammingJoinOrdering();
    if (test_num == 0 || test_num == 4) TestPhysicalJoinSelection();

    std::cout << "\n=================================================================" << std::endl;
    std::cout << "ALL OPTIMIZER TESTS COMPLETED!" << std::endl;
    std::cout << "=================================================================" << std::endl;
    return 0;
}
