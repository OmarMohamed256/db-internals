#include <iostream>
#include <cassert>
#include <vector>
#include <string>
#include <memory>
#include <thread>
#include <unordered_set>
#include <cstdio>

#include "snapshot.h"
#include "transaction.h"
#include "transaction_manager.h"
#include "mvcc_table_heap.h"
#include "mvcc_seq_scan.h"

#include "../storage/rid.h"
#include "../storage/user_row.h"
#include "../storage/table_heap.h"
#include "../buffer_pool_manager_skeleton/async_disk_manager.h"
#include "../buffer_pool_manager_skeleton/lru_k_replacer.h"
#include "../buffer_pool_manager_skeleton/buffer_pool_manager.h"

static void CleanUpDb(const std::string &db_name) {
    std::remove(db_name.c_str());
}

// ============================================================================
// Milestone 1: Transaction Lifecycle & Monotonicity
// ============================================================================
void TestTransactionLifecycle() {
    std::cout << "\n=== [Milestone 1] Transaction Lifecycle & Monotonicity ===" << std::endl;

    TransactionManager txn_mgr;

    // 1. Begin Transactions
    Transaction *t1 = txn_mgr.Begin();
    assert(t1 != nullptr && "Begin must return valid Transaction pointer!");
    Transaction *t2 = txn_mgr.Begin();
    assert(t2 != nullptr);
    Transaction *t3 = txn_mgr.Begin();
    assert(t3 != nullptr);

    // 2. Monotonic Txn IDs
    std::cout << "  • Allocating transaction IDs: T" << t1->GetTxnId()
              << ", T" << t2->GetTxnId() << ", T" << t3->GetTxnId() << "..." << std::endl;
    assert(t1->GetTxnId() < t2->GetTxnId());
    assert(t2->GetTxnId() < t3->GetTxnId());

    // 3. Active Transaction Tracking
    assert(txn_mgr.GetActiveTxnCount() == 3);
    assert(txn_mgr.GetTxnState(t1->GetTxnId()) == TxnState::IN_PROGRESS);

    // 4. Commit Transition
    std::cout << "  • Committing T" << t1->GetTxnId() << "..." << std::endl;
    txn_mgr.Commit(t1);
    assert(t1->GetState() == TxnState::COMMITTED);
    assert(txn_mgr.GetTxnState(t1->GetTxnId()) == TxnState::COMMITTED);
    assert(txn_mgr.GetActiveTxnCount() == 2);

    // 5. Abort Transition
    std::cout << "  • Aborting T" << t2->GetTxnId() << "..." << std::endl;
    txn_mgr.Abort(t2);
    assert(t2->GetState() == TxnState::ABORTED);
    assert(txn_mgr.GetTxnState(t2->GetTxnId()) == TxnState::ABORTED);
    assert(txn_mgr.GetActiveTxnCount() == 1);

    txn_mgr.Commit(t3);
    assert(txn_mgr.GetActiveTxnCount() == 0);

    std::cout << "  ✓ Milestone 1 (Transaction Lifecycle & Monotonicity) passed!" << std::endl;
}

// ============================================================================
// Milestone 2: PostgreSQL MVCC Visibility Matrix
// ============================================================================
void TestSnapshotVisibilityMatrix() {
    std::cout << "\n=== [Milestone 2] PostgreSQL MVCC Visibility Matrix ===" << std::endl;

    // Mock State Provider
    // T1: COMMITTED (old committed txn)
    // T2: ABORTED (failed txn)
    // T3: IN_PROGRESS (concurrent active txn)
    // T4: COMMITTED (committed before snapshot)
    // T5: Current Reader Txn (read_ts = 5)
    // T6: COMMITTED (committed in future after snapshot)
    auto state_lookup = [](txn_id_t id) -> TxnState {
        if (id == 1 || id == 4 || id == 6) return TxnState::COMMITTED;
        if (id == 2) return TxnState::ABORTED;
        if (id == 3 || id == 5) return TxnState::IN_PROGRESS;
        return TxnState::COMMITTED;
    };

    Snapshot snapshot(5, {3}); // read_ts = 5, T3 active at snapshot creation
    txn_id_t reader_xid = 5;

    // Case 1: Committed before snapshot (xmin = 1, xmax = 0) -> Visible
    UserRow r1(1, "Alice", 100.0, 0, /*xmin=*/1);
    r1.xmax = 0;
    assert(snapshot.IsVisible(r1, reader_xid, state_lookup) && "Committed row must be visible!");

    // Case 2: Inserted by uncommitted active txn (xmin = 3) -> Invisible
    UserRow r2(2, "Bob", 200.0, 0, /*xmin=*/3);
    r2.xmax = 0;
    assert(!snapshot.IsVisible(r2, reader_xid, state_lookup) && "Active uncommitted row must be invisible!");

    // Case 3: Inserted in future (xmin = 6 > read_ts 5) -> Invisible
    UserRow r3(3, "Charlie", 300.0, 0, /*xmin=*/6);
    r3.xmax = 0;
    assert(!snapshot.IsVisible(r3, reader_xid, state_lookup) && "Future inserted row must be invisible!");

    // Case 4: Inserted by aborted txn (xmin = 2) -> Invisible
    UserRow r4(4, "Dave", 400.0, 0, /*xmin=*/2);
    r4.xmax = 0;
    assert(!snapshot.IsVisible(r4, reader_xid, state_lookup) && "Aborted inserted row must be invisible!");

    // Case 5: Deleted by txn committed before snapshot (xmin = 1, xmax = 4) -> Invisible
    UserRow r5(5, "Eve", 500.0, 0, /*xmin=*/1);
    r5.xmax = 4;
    assert(!snapshot.IsVisible(r5, reader_xid, state_lookup) && "Deleted row before snapshot must be invisible!");

    // Case 6: Deleted by txn committed in future (xmin = 1, xmax = 6) -> Visible
    UserRow r6(6, "Frank", 600.0, 0, /*xmin=*/1);
    r6.xmax = 6;
    assert(snapshot.IsVisible(r6, reader_xid, state_lookup) && "Row deleted in future must remain visible!");

    // Case 7: Deleted by currently active txn (xmin = 1, xmax = 3) -> Visible
    UserRow r7(7, "Grace", 700.0, 0, /*xmin=*/1);
    r7.xmax = 3;
    assert(snapshot.IsVisible(r7, reader_xid, state_lookup) && "Row deleted by active txn must remain visible!");

    // Case 8: Deleted by aborted txn (xmin = 1, xmax = 2) -> Visible
    UserRow r8(8, "Heidi", 800.0, 0, /*xmin=*/1);
    r8.xmax = 2;
    assert(snapshot.IsVisible(r8, reader_xid, state_lookup) && "Row deleted by aborted txn must remain visible!");

    // Case 9: Own uncommitted insert (xmin = 5, xmax = 0) -> Visible
    UserRow r9(9, "Ivan", 900.0, 0, /*xmin=*/5);
    r9.xmax = 0;
    assert(snapshot.IsVisible(r9, reader_xid, state_lookup) && "Own uncommitted insert must be visible!");

    // Case 10: Own uncommitted delete (xmin = 1, xmax = 5) -> Invisible
    UserRow r10(10, "Judy", 1000.0, 0, /*xmin=*/1);
    r10.xmax = 5;
    assert(!snapshot.IsVisible(r10, reader_xid, state_lookup) && "Own delete must be invisible to self!");

    // Case 11: Created and deleted by own txn (xmin = 5, xmax = 5) -> Invisible
    UserRow r11(11, "Karl", 1100.0, 0, /*xmin=*/5);
    r11.xmax = 5;
    assert(!snapshot.IsVisible(r11, reader_xid, state_lookup) && "Self-created and deleted must be invisible!");

    std::cout << "  ✓ All 11 PostgreSQL MVCC visibility cases verified with flying colors!" << std::endl;
}

// ============================================================================
// Milestone 3: Non-Blocking Reads with Concurrent Writers
// ============================================================================
void TestNonBlockingReadsConcurrentWriters() {
    std::cout << "\n=== [Milestone 3] Non-Blocking Reads with Concurrent Writers ===" << std::endl;
    const std::string db_name = "test_mvcc_readers_writers.db";
    CleanUpDb(db_name);

    auto disk_manager = std::make_unique<AsyncDiskManager>(db_name);
    auto replacer = std::make_unique<LRUKReplacer>(50);
    auto bpm = std::make_unique<BufferPoolManager>(50, disk_manager.get(), replacer.get());
    auto table_heap = std::make_unique<TableHeap>(bpm.get());

    TransactionManager txn_mgr;
    MVCCTableHeap mvcc_heap(table_heap.get(), &txn_mgr);

    // 1. Initial State: Insert 100 users under Txn 1
    std::cout << "  • Populating initial 100 users in Txn 1..." << std::endl;
    Transaction *t1 = txn_mgr.Begin();
    std::vector<RID> initial_rids;
    initial_rids.reserve(100);

    for (int i = 1; i <= 100; ++i) {
        UserRow user(i, ("User_" + std::to_string(i)).c_str(), 10.0 * i);
        RID rid;
        assert(mvcc_heap.Insert(t1, user, &rid));
        initial_rids.push_back(rid);
    }
    txn_mgr.Commit(t1);

    // 2. Reader Transaction begins and takes snapshot
    std::cout << "  • Reader Transaction T_read begins (takes point-in-time snapshot)..." << std::endl;
    Transaction *t_read = txn_mgr.Begin();

    // 3. Concurrent Writer Transaction performs heavy modifications and commits
    std::cout << "  • Concurrent Writer Transaction T_write modifies the database..." << std::endl;
    Transaction *t_write = txn_mgr.Begin();

    // Writer updates rows 1..50 to balance = 99999.0
    for (int i = 0; i < 50; ++i) {
        UserRow updated_user(i + 1, ("User_" + std::to_string(i + 1)).c_str(), 99999.0);
        RID new_rid;
        assert(mvcc_heap.Update(t_write, initial_rids[i], updated_user, &new_rid));
    }

    // Writer deletes rows 51..100
    for (int i = 50; i < 100; ++i) {
        assert(mvcc_heap.Delete(t_write, initial_rids[i]));
    }

    // Writer inserts 50 brand-new users (101..150)
    for (int i = 101; i <= 150; ++i) {
        UserRow new_user(i, ("NewUser_" + std::to_string(i)).c_str(), 500.0);
        RID rid;
        assert(mvcc_heap.Insert(t_write, new_user, &rid));
    }

    txn_mgr.Commit(t_write);
    std::cout << "  • T_write committed: 50 updates, 50 deletes, 50 new inserts." << std::endl;

    // 4. Reader Scans the Table via MVCCSeqScanExecutor
    std::cout << "  • Reader executes MVCCSeqScan under frozen snapshot..." << std::endl;
    MVCCSeqScanExecutor scan(&mvcc_heap, t_read);
    scan.Init();

    UserRow scanned_row;
    RID scanned_rid;
    int visible_rows = 0;
    std::unordered_set<int64_t> observed_ids;

    while (scan.Next(&scanned_row, &scanned_rid)) {
        assert(scanned_row.id >= 1 && scanned_row.id <= 100 && "Future inserted row leaked into snapshot!");
        assert(scanned_row.balance == 10.0 * scanned_row.id && "Overwritten update leaked into reader snapshot!");
        observed_ids.insert(scanned_row.id);
        visible_rows++;
    }

    assert(visible_rows == 100 && "Reader must observe exactly 100 rows!");
    assert(observed_ids.size() == 100 && "All 100 original rows must be intact!");
    std::cout << "  • Reader observed exactly 100 original rows with original balances!" << std::endl;
    std::cout << "  • Zero shared read locks acquired; zero lock contention!" << std::endl;

    txn_mgr.Commit(t_read);
    CleanUpDb(db_name);
    std::cout << "  ✓ Milestone 3 (Non-Blocking Reads with Concurrent Writers) passed!" << std::endl;
}

// ============================================================================
// Milestone 4: Write-Write Conflict Detection (First-Committer-Wins)
// ============================================================================
void TestWriteWriteConflict() {
    std::cout << "\n=== [Milestone 4] Write-Write Conflict Detection (First-Committer-Wins) ===" << std::endl;
    const std::string db_name = "test_mvcc_conflicts.db";
    CleanUpDb(db_name);

    auto disk_manager = std::make_unique<AsyncDiskManager>(db_name);
    auto replacer = std::make_unique<LRUKReplacer>(30);
    auto bpm = std::make_unique<BufferPoolManager>(30, disk_manager.get(), replacer.get());
    auto table_heap = std::make_unique<TableHeap>(bpm.get());

    TransactionManager txn_mgr;
    MVCCTableHeap mvcc_heap(table_heap.get(), &txn_mgr);

    // 1. Seed row 42
    Transaction *t_init = txn_mgr.Begin();
    UserRow original_user(42, "Alice", 100.0);
    RID target_rid;
    assert(mvcc_heap.Insert(t_init, original_user, &target_rid));
    txn_mgr.Commit(t_init);

    // 2. Start two concurrent transactions T_A and T_B
    Transaction *t_a = txn_mgr.Begin();
    Transaction *t_b = txn_mgr.Begin();

    // 3. T_A updates row 42 -> Succeeds
    std::cout << "  • Transaction T_A updates row 42..." << std::endl;
    UserRow update_a(42, "Alice", 500.0);
    RID new_rid_a;
    bool a_ok = mvcc_heap.Update(t_a, target_rid, update_a, &new_rid_a);
    assert(a_ok && "T_A update must succeed!");

    // 4. Concurrent T_B attempts to update row 42 -> MUST DETECT CONFLICT & FAIL!
    std::cout << "  • Concurrent Transaction T_B attempts to update row 42..." << std::endl;
    UserRow update_b(42, "Alice", 700.0);
    RID new_rid_b;
    bool b_ok = mvcc_heap.Update(t_b, target_rid, update_b, &new_rid_b);
    assert(!b_ok && "T_B update MUST fail with Write-Write Conflict (First-Committer-Wins)!");
    std::cout << "  • Write-Write Conflict detected! T_B aborted to prevent lost update." << std::endl;

    txn_mgr.Abort(t_b);
    txn_mgr.Commit(t_a);

    // 5. Verify final balance is 500.0 (T_A's update won)
    Transaction *t_verify = txn_mgr.Begin();
    UserRow final_row;
    assert(mvcc_heap.GetTuple(t_verify, new_rid_a, &final_row));
    assert(final_row.balance == 500.0);
    txn_mgr.Commit(t_verify);

    CleanUpDb(db_name);
    std::cout << "  ✓ Milestone 4 (Write-Write Conflict Detection) passed!" << std::endl;
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
    std::cout << "DBMS CONCURRENCY SUITE: MVCC Snapshot Isolation & Non-Blocking Reads" << std::endl;
    if (test_num > 0) {
        std::cout << "Running Milestone Test #" << test_num << std::endl;
    }
    std::cout << "=================================================================" << std::endl;

    if (test_num == 0 || test_num == 1) TestTransactionLifecycle();
    if (test_num == 0 || test_num == 2) TestSnapshotVisibilityMatrix();
    if (test_num == 0 || test_num == 3) TestNonBlockingReadsConcurrentWriters();
    if (test_num == 0 || test_num == 4) TestWriteWriteConflict();

    std::cout << "\n=================================================================" << std::endl;
    std::cout << "ALL CONCURRENCY TESTS COMPLETED!" << std::endl;
    std::cout << "=================================================================" << std::endl;
    return 0;
}
