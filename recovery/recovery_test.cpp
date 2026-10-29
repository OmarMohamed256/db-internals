#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "log_manager.h"
#include "log_record.h"
#include "recovery_engine.h"
#include "../buffer_pool_manager_skeleton/async_disk_manager.cpp"
#include "../buffer_pool_manager_skeleton/buffer_pool_manager.cpp"
#include "../buffer_pool_manager_skeleton/disk_manager.cpp"
#include "../buffer_pool_manager_skeleton/io_uring_manager.cpp"
#include "../buffer_pool_manager_skeleton/lru_k_replacer.cpp"
#include "../storage/table_heap.cpp"

static void CleanUpFiles(const std::string &db_name, const std::string &wal_name, const std::string &meta_name = "") {
    std::remove(db_name.c_str());
    std::remove(wal_name.c_str());
    if (!meta_name.empty()) {
        std::remove(meta_name.c_str());
    }
}

// ============================================================================
// Milestone 1: Binary LogRecord Serialization & Deserialization
// ============================================================================
void TestLogRecordSerialization() {
    std::cout << "\n=== [Milestone 1] Binary LogRecord Serialization & Deserialization ===" << std::endl;

    // 1. Transaction lifecycle record (BEGIN)
    LogRecord begin_rec(/*txn_id=*/10, /*prev_lsn=*/INVALID_LSN, LogRecordType::BEGIN);
    begin_rec.SetLSN(101);
    assert(begin_rec.GetSize() == LogRecord::HEADER_SIZE);

    std::vector<char> buf(512);
    size_t written = begin_rec.Serialize(buf.data());
    assert(written == LogRecord::HEADER_SIZE);

    LogRecord parsed_begin;
    assert(LogRecord::Deserialize(buf.data(), written, &parsed_begin));
    assert(parsed_begin.lsn == 101);
    assert(parsed_begin.txn_id == 10);
    assert(parsed_begin.prev_lsn == INVALID_LSN);
    assert(parsed_begin.type == LogRecordType::BEGIN);
    std::cout << "  • BEGIN record serialization verified: " << parsed_begin.ToString() << std::endl;

    // 2. Tuple Update record (with before and after image)
    UserRow before_row(42, "Alice_Old", 100.0, 1000, 10);
    UserRow after_row(42, "Alice_New", 250.0, 2000, 10);

    LogRecord update_rec(/*txn_id=*/10, /*prev_lsn=*/101, LogRecordType::UPDATE,
                         /*page_id=*/3, /*slot_num=*/7, before_row, after_row);
    update_rec.SetLSN(102);

    size_t expected_update_size = LogRecord::HEADER_SIZE + sizeof(page_id_t) + sizeof(uint32_t) + (2 * sizeof(UserRow));
    assert(update_rec.GetSize() == expected_update_size);

    written = update_rec.Serialize(buf.data());
    assert(written == expected_update_size);

    LogRecord parsed_update;
    assert(LogRecord::Deserialize(buf.data(), written, &parsed_update));
    assert(parsed_update.lsn == 102);
    assert(parsed_update.prev_lsn == 101);
    assert(parsed_update.txn_id == 10);
    assert(parsed_update.type == LogRecordType::UPDATE);
    assert(parsed_update.page_id == 3);
    assert(parsed_update.slot_num == 7);
    assert(parsed_update.before_image == before_row);
    assert(parsed_update.after_image == after_row);
    std::cout << "  • UPDATE record serialization verified: " << parsed_update.ToString() << std::endl;

    // 3. ARIES CLR (Compensation Log Record)
    LogRecord clr_rec(/*txn_id=*/10, /*prev_lsn=*/102, /*undo_next_lsn=*/101,
                      /*page_id=*/3, /*slot_num=*/7, before_row);
    clr_rec.SetLSN(103);
    written = clr_rec.Serialize(buf.data());
    LogRecord parsed_clr;
    assert(LogRecord::Deserialize(buf.data(), written, &parsed_clr));
    assert(parsed_clr.type == LogRecordType::CLR);
    assert(parsed_clr.undo_next_lsn == 101);
    assert(parsed_clr.after_image == before_row);
    std::cout << "  • ARIES CLR record serialization verified: " << parsed_clr.ToString() << std::endl;

    std::cout << "  ✓ Milestone 1 (Binary LogRecord Serialization) passed!" << std::endl;
}

// ============================================================================
// Milestone 2: Mechanical Sympathy Group Commit & Natural Batching
// ============================================================================
void TestMechanicalSympathyGroupCommit() {
    std::cout << "\n=== [Milestone 2] Mechanical Sympathy Group Commit & Natural Batching ===" << std::endl;
    const std::string wal_name = "test_group_commit.wal";
    std::remove(wal_name.c_str());

    {
        LogManager log_mgr(wal_name);
        const int num_threads = 16;
        const int txns_per_thread = 500;
        const int total_txns = num_threads * txns_per_thread;

        std::cout << "  • Launching " << num_threads << " concurrent threads committing "
                  << total_txns << " total transactions..." << std::endl;

        auto start_time = std::chrono::high_resolution_clock::now();
        std::vector<std::thread> workers;
        workers.reserve(num_threads);

        for (int t = 0; t < num_threads; ++t) {
            workers.emplace_back([&log_mgr, t, txns_per_thread]() {
                for (int i = 0; i < txns_per_thread; ++i) {
                    txn_id_t tid = (t * txns_per_thread) + i + 1;
                    LogRecord rec(tid, INVALID_LSN, LogRecordType::COMMIT);
                    lsn_t lsn = log_mgr.AppendRecord(&rec);
                    log_mgr.Flush(lsn);
                }
            });
        }

        for (auto &w : workers) {
            w.join();
        }

        auto end_time = std::chrono::high_resolution_clock::now();
        double elapsed_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();

        size_t total_flushes = log_mgr.GetFlushCount();
        assert(total_flushes > 0);
        double avg_batch_size = static_cast<double>(total_txns) / total_flushes;
        double throughput = (total_txns / (elapsed_ms / 1000.0));

        std::cout << "  • Total Commits:        " << total_txns << std::endl;
        std::cout << "  • Physical Disk Flushes:" << total_flushes << " (fsync calls)" << std::endl;
        std::cout << "  • Natural Batch Factor: " << avg_batch_size << " txns/flush (Mechanical Sympathy!)" << std::endl;
        std::cout << "  • Elapsed Time:         " << elapsed_ms << " ms" << std::endl;
        std::cout << "  • Commit Throughput:    " << static_cast<int>(throughput) << " txns/sec" << std::endl;

        assert(total_flushes < static_cast<size_t>(total_txns) &&
               "Natural batching must group commits to avoid disk queue stalls!");
        assert(log_mgr.GetFlushedLSN() >= total_txns);
    }

    std::remove(wal_name.c_str());
    std::cout << "  ✓ Milestone 2 (Mechanical Sympathy Group Commit) passed!" << std::endl;
}

// ============================================================================
// Milestone 3: The Cardinal WAL Flush Invariant
// ============================================================================
void TestCardinalWALInvariant() {
    std::cout << "\n=== [Milestone 3] The Cardinal WAL Flush Invariant ===" << std::endl;
    const std::string wal_name = "test_invariant.wal";
    std::remove(wal_name.c_str());

    LogManager log_mgr(wal_name);

    // 1. Simulate in-memory buffer page update with LSN 50
    uint64_t page_lsn = 50;

    // Initially, nothing has been flushed
    assert(log_mgr.GetFlushedLSN() == 0);

    // INVARIANT CHECK: Can page hit disk?
    bool can_flush_page = (page_lsn <= static_cast<uint64_t>(log_mgr.GetFlushedLSN()));
    assert(!can_flush_page && "DIRTY PAGE CANNOT HIT DISK BEFORE WAL RECORD FLUSHES!");
    std::cout << "  • Invariant enforced: page_lsn (50) > flushed_lsn (0) -> Disk write BLOCKED." << std::endl;

    // 2. Transaction appends records up to LSN 50 and flushes WAL
    for (int i = 1; i <= 50; ++i) {
        LogRecord rec(1, INVALID_LSN, LogRecordType::INSERT, 1, i, UserRow(i, "User", 10.0));
        log_mgr.AppendRecord(&rec);
    }
    log_mgr.Flush(50);
    assert(log_mgr.GetFlushedLSN() >= 50);

    // 3. Now verify invariant permits page flush
    can_flush_page = (page_lsn <= static_cast<uint64_t>(log_mgr.GetFlushedLSN()));
    assert(can_flush_page && "WAL record is durable; page is now safe to write to disk!");
    std::cout << "  • Invariant satisfied: page_lsn (50) <= flushed_lsn ("
              << log_mgr.GetFlushedLSN() << ") -> Disk write PERMITTED." << std::endl;

    std::remove(wal_name.c_str());
    std::cout << "  ✓ Milestone 3 (Cardinal WAL Invariant) passed!" << std::endl;
}

// ============================================================================
// Milestone 4: End-to-End ARIES Recovery (Analysis, 3-Tier Redo, Undo with CLRs)
// ============================================================================
void TestCrashRecoveryRedoReplay() {
    std::cout << "\n=== [Milestone 4] 3-Phase ARIES Recovery (Analysis, Redo, Undo) ===" << std::endl;
    const std::string db_name = "test_aries_db.db";
    const std::string wal_name = "test_aries_db.wal";
    CleanUpFiles(db_name, wal_name);

    std::vector<RID> inserted_rids;

    // ------------------------------------------------------------------------
    // Phase 1 Workload: Committed T1 + Uncommitted Loser T2
    // ------------------------------------------------------------------------
    {
        std::cout << "  • Step 1: Populating 100 users under Committed Txn T1..." << std::endl;
        auto disk_mgr = std::make_unique<AsyncDiskManager>(db_name);
        auto replacer = std::make_unique<LRUKReplacer>(100);
        auto bpm = std::make_unique<BufferPoolManager>(100, disk_mgr.get(), replacer.get());
        auto table_heap = std::make_unique<TableHeap>(bpm.get());
        LogManager log_mgr(wal_name);

        txn_id_t t1 = 1;
        LogRecord begin_t1(t1, INVALID_LSN, LogRecordType::BEGIN);
        lsn_t prev_l1 = log_mgr.AppendRecord(&begin_t1);

        for (int i = 1; i <= 100; ++i) {
            UserRow row(i, ("User_" + std::to_string(i)).c_str(), 10.0 * i);
            RID rid;
            assert(table_heap->InsertTuple(row, &rid));
            inserted_rids.push_back(rid);

            LogRecord ins_rec(t1, prev_l1, LogRecordType::INSERT, rid.page_id, rid.slot_num, row);
            prev_l1 = log_mgr.AppendRecord(&ins_rec);

            Page *page = bpm->FetchPage(rid.page_id);
            auto *tp = reinterpret_cast<TablePage *>(page->GetData());
            tp->SetLSN(prev_l1);
            bpm->UnpinPage(rid.page_id, true);
        }

        // T1 Commits:
        LogRecord commit_t1(t1, prev_l1, LogRecordType::COMMIT);
        lsn_t commit_lsn = log_mgr.AppendRecord(&commit_t1);
        log_mgr.Flush(commit_lsn);

        // Loser Transaction T2 starts, modifies rows 1..10, but NEVER commits before crash!
        std::cout << "  • Step 2: Loser Txn T2 modifies rows 1..10, but crashes without commit..." << std::endl;
        txn_id_t t2 = 2;
        LogRecord begin_t2(t2, INVALID_LSN, LogRecordType::BEGIN);
        lsn_t prev_l2 = log_mgr.AppendRecord(&begin_t2);

        for (int i = 0; i < 10; ++i) {
            UserRow old_row(i + 1, ("User_" + std::to_string(i + 1)).c_str(), 10.0 * (i + 1));
            UserRow dirty_row(i + 1, ("Hacked_" + std::to_string(i + 1)).c_str(), 999999.0);

            // Apply in memory
            Page *page = bpm->FetchPage(inserted_rids[i].page_id);
            auto *tp = reinterpret_cast<TablePage *>(page->GetData());
            tp->UpdateTuple(inserted_rids[i].slot_num, dirty_row);

            LogRecord upd_rec(t2, prev_l2, LogRecordType::UPDATE,
                              inserted_rids[i].page_id, inserted_rids[i].slot_num,
                              old_row, dirty_row);
            prev_l2 = log_mgr.AppendRecord(&upd_rec);
            tp->SetLSN(prev_l2);
            bpm->UnpinPage(inserted_rids[i].page_id, true);
        }

        // Flushes WAL up to T2's updates, but T2 never committed!
        log_mgr.Flush(prev_l2);

        std::cout << "  • SIMULATING PROCESS CRASH (SIGKILL): T1 committed, T2 dirty uncommitted!" << std::endl;
    }

    // ------------------------------------------------------------------------
    // Step 2: System Restarts & Executes Full 3-Phase ARIES Recovery
    // ------------------------------------------------------------------------
    {
        std::cout << "  • Step 3: System restart... executing 3-Phase ARIES Recovery..." << std::endl;
        auto disk_mgr = std::make_unique<AsyncDiskManager>(db_name);
        auto replacer = std::make_unique<LRUKReplacer>(100);
        auto bpm = std::make_unique<BufferPoolManager>(100, disk_mgr.get(), replacer.get());
        LogManager recovery_log_mgr(wal_name);

        RecoveryEngine aries(bpm.get(), wal_name, &recovery_log_mgr);

        // 1. Analysis Phase
        aries.Phase1Analysis();
        const auto &att = aries.GetATT();
        const auto &dpt = aries.GetDPT();

        std::cout << "  • [Analysis Phase] ATT identifies " << att.size() << " active loser txn(s)." << std::endl;
        assert(att.find(2) != att.end() && "T2 must be identified as an active loser transaction!");
        assert(att.find(1) == att.end() && "Committed T1 must NOT be in ATT!");

        std::cout << "  • [Analysis Phase] DPT identifies " << dpt.size() << " dirty page(s)." << std::endl;
        assert(!dpt.empty());
        std::cout << "  • [Analysis Phase] Earliest unwritten LSN (min_recLSN): " << aries.GetMinRecLSN() << std::endl;

        // 2. Redo Phase (Repeating History with 3-Tier Filter)
        size_t redone = aries.Phase2Redo();
        std::cout << "  • [Redo Phase] Replayed " << redone << " operations. Disk I/O skipped: "
                  << aries.GetDiskIOSkippedCount() << " check(s) via DPT/recLSN filters!" << std::endl;

        // 3. Undo Phase (Rolling back Loser T2 with CLRs)
        size_t undone = aries.Phase3Undo();
        std::cout << "  • [Undo Phase] Reversed " << undone << " uncommitted operations of loser T2 via CLRs!" << std::endl;
        assert(undone == 10 && "All 10 uncommitted writes of T2 must be undone!");

        // --------------------------------------------------------------------
        // Step 4: Validate Restored Database State
        // --------------------------------------------------------------------
        auto table_heap = std::make_unique<TableHeap>(bpm.get(), inserted_rids[0].page_id);
        for (int i = 1; i <= 100; ++i) {
            UserRow row;
            assert(table_heap->GetTuple(inserted_rids[i - 1], &row));
            // T2's dirty updates (999999.0) must be completely rolled back!
            assert(row.balance == 10.0 * i && "Loser uncommitted updates must be completely reversed!");
            std::string expected_name = "User_" + std::to_string(i);
            assert(std::string(row.name) == expected_name && "Original row content must be restored!");
        }

        std::cout << "  • Verification: All 100 committed rows restored; all loser writes completely undone!" << std::endl;
    }

    CleanUpFiles(db_name, wal_name);
    std::cout << "  ✓ Milestone 4 (3-Phase ARIES Recovery) passed with flying colors!" << std::endl;
}

// ============================================================================
// Milestone 5: ARIES Fuzzy Checkpointing & Bounded Recovery
// ============================================================================
void TestFuzzyCheckpointRecovery() {
    std::cout << "\n=== [Milestone 5] ARIES Fuzzy Checkpointing & Bounded Recovery ===" << std::endl;
    const std::string db_name = "test_ckpt_db.db";
    const std::string wal_name = "test_ckpt_db.wal";
    const std::string meta_name = "checkpoint.meta";
    CleanUpFiles(db_name, wal_name, meta_name);

    std::vector<RID> inserted_rids;
    lsn_t ckpt_begin_lsn = INVALID_LSN;

    // ------------------------------------------------------------------------
    // Step 1 & 2: Baseline transactions T1..T5 and Fuzzy Checkpoint
    // ------------------------------------------------------------------------
    {
        std::cout << "  • Step 1: Populating baseline transactions T1..T5..." << std::endl;
        auto disk_mgr = std::make_unique<AsyncDiskManager>(db_name);
        auto replacer = std::make_unique<LRUKReplacer>(100);
        auto bpm = std::make_unique<BufferPoolManager>(100, disk_mgr.get(), replacer.get());
        auto table_heap = std::make_unique<TableHeap>(bpm.get());
        LogManager log_mgr(wal_name);

        // Transactions T1..T4 insert rows and commit:
        for (int t = 1; t <= 4; ++t) {
            txn_id_t tid = t;
            LogRecord begin_rec(tid, INVALID_LSN, LogRecordType::BEGIN);
            lsn_t lsn = log_mgr.AppendRecord(&begin_rec);

            UserRow row(t, ("User_" + std::to_string(t)).c_str(), 100.0 * t);
            RID rid;
            assert(table_heap->InsertTuple(row, &rid));
            inserted_rids.push_back(rid);

            LogRecord ins_rec(tid, lsn, LogRecordType::INSERT, rid.page_id, rid.slot_num, row);
            lsn = log_mgr.AppendRecord(&ins_rec);

            Page *page = bpm->FetchPage(rid.page_id);
            auto *tp = reinterpret_cast<TablePage *>(page->GetData());
            tp->SetLSN(lsn);
            bpm->UnpinPage(rid.page_id, true);

            LogRecord commit_rec(tid, lsn, LogRecordType::COMMIT);
            lsn = log_mgr.AppendRecord(&commit_rec);
            log_mgr.Flush(lsn);
        }

        // Transaction T5 begins and inserts row 5, but is active at checkpoint time!
        txn_id_t t5 = 5;
        LogRecord begin_t5(t5, INVALID_LSN, LogRecordType::BEGIN);
        lsn_t prev_l5 = log_mgr.AppendRecord(&begin_t5);

        UserRow row5(5, "User_5", 500.0);
        RID rid5;
        assert(table_heap->InsertTuple(row5, &rid5));
        inserted_rids.push_back(rid5);

        LogRecord ins_t5(t5, prev_l5, LogRecordType::INSERT, rid5.page_id, rid5.slot_num, row5);
        prev_l5 = log_mgr.AppendRecord(&ins_t5);

        Page *page5 = bpm->FetchPage(rid5.page_id);
        auto *tp5 = reinterpret_cast<TablePage *>(page5->GetData());
        tp5->SetLSN(prev_l5);
        bpm->UnpinPage(rid5.page_id, true);

        // Step 2: Checkpoint generation
        std::cout << "  • Step 2: Generating ARIES Fuzzy Checkpoint..." << std::endl;
        RecoveryEngine engine(bpm.get(), wal_name, &log_mgr);
        std::unordered_map<txn_id_t, lsn_t> att_snap = {{t5, prev_l5}};
        std::unordered_map<page_id_t, lsn_t> dpt_snap = {{inserted_rids[0].page_id, 2}};
        ckpt_begin_lsn = engine.CreateCheckpoint(att_snap, dpt_snap, meta_name);
        assert(ckpt_begin_lsn != INVALID_LSN);
        std::cout << "  • Step 3: Persisted Master Record: checkpoint_begin_lsn = " << ckpt_begin_lsn << std::endl;

        // Step 4: Post-checkpoint transactions T6 (committed) & T7 (uncommitted loser)
        std::cout << "  • Step 4: Running post-checkpoint transactions T6 (committed) & T7 (uncommitted loser)..." << std::endl;

        // T5 now commits:
        LogRecord commit_t5(t5, prev_l5, LogRecordType::COMMIT);
        lsn_t l5_commit = log_mgr.AppendRecord(&commit_t5);
        log_mgr.Flush(l5_commit);

        // T6 starts, inserts row 6, commits:
        txn_id_t t6 = 6;
        LogRecord begin_t6(t6, INVALID_LSN, LogRecordType::BEGIN);
        lsn_t prev_l6 = log_mgr.AppendRecord(&begin_t6);

        UserRow row6(6, "User_6", 600.0);
        RID rid6;
        assert(table_heap->InsertTuple(row6, &rid6));
        inserted_rids.push_back(rid6);

        LogRecord ins_t6(t6, prev_l6, LogRecordType::INSERT, rid6.page_id, rid6.slot_num, row6);
        prev_l6 = log_mgr.AppendRecord(&ins_t6);

        Page *page6 = bpm->FetchPage(rid6.page_id);
        auto *tp6 = reinterpret_cast<TablePage *>(page6->GetData());
        tp6->SetLSN(prev_l6);
        bpm->UnpinPage(rid6.page_id, true);

        LogRecord commit_t6(t6, prev_l6, LogRecordType::COMMIT);
        lsn_t l6_commit = log_mgr.AppendRecord(&commit_t6);
        log_mgr.Flush(l6_commit);

        // T7 starts, updates row 1 to corrupt value, but never commits!
        txn_id_t t7 = 7;
        LogRecord begin_t7(t7, INVALID_LSN, LogRecordType::BEGIN);
        lsn_t prev_l7 = log_mgr.AppendRecord(&begin_t7);

        UserRow old_row1(1, "User_1", 100.0);
        UserRow dirty_row1(1, "Corrupted_1", 999999.0);

        Page *page1 = bpm->FetchPage(inserted_rids[0].page_id);
        auto *tp1 = reinterpret_cast<TablePage *>(page1->GetData());
        tp1->UpdateTuple(inserted_rids[0].slot_num, dirty_row1);

        LogRecord upd_t7(t7, prev_l7, LogRecordType::UPDATE,
                         inserted_rids[0].page_id, inserted_rids[0].slot_num,
                         old_row1, dirty_row1);
        prev_l7 = log_mgr.AppendRecord(&upd_t7);
        tp1->SetLSN(prev_l7);
        bpm->UnpinPage(inserted_rids[0].page_id, true);

        log_mgr.Flush(prev_l7);

        std::cout << "  • SIMULATING PROCESS CRASH!" << std::endl;
    }

    // ------------------------------------------------------------------------
    // Step 5: System Restart with Checkpoint-Aware ARIES Recovery
    // ------------------------------------------------------------------------
    {
        std::cout << "  • Step 5: System restart with Checkpoint-Aware ARIES Recovery..." << std::endl;
        auto disk_mgr = std::make_unique<AsyncDiskManager>(db_name);
        auto replacer = std::make_unique<LRUKReplacer>(100);
        auto bpm = std::make_unique<BufferPoolManager>(100, disk_mgr.get(), replacer.get());
        LogManager recovery_log_mgr(wal_name);

        RecoveryEngine aries(bpm.get(), wal_name, &recovery_log_mgr);

        // Verify Master Record
        lsn_t read_ckpt_lsn = INVALID_LSN;
        std::ifstream meta_check(meta_name, std::ios::binary);
        assert(meta_check.is_open());
        meta_check.read(reinterpret_cast<char *>(&read_ckpt_lsn), sizeof(lsn_t));
        meta_check.close();
        assert(read_ckpt_lsn == ckpt_begin_lsn);
        std::cout << "  • [Master Record] Found latest checkpoint at LSN " << read_ckpt_lsn << "." << std::endl;

        // Run Analysis
        aries.Phase1Analysis(meta_name);
        size_t bypassed = aries.GetCheckpointBypassedCount();
        std::cout << "  • [Analysis Phase] Initialized ATT & DPT from CHECKPOINT_END (bypassed "
                  << bypassed << " pre-checkpoint records!)." << std::endl;
        assert(bypassed > 0 && "Checkpoint-aware analysis MUST bypass records prior to checkpoint!");

        const auto &att = aries.GetATT();
        const auto &dpt = aries.GetDPT();

        std::cout << "  • [Analysis Phase] Scanned post-checkpoint records. Identified loser T7 (ATT size: "
                  << att.size() << ")." << std::endl;
        assert(att.find(7) != att.end() && "T7 must be identified as an active loser transaction!");
        assert(att.find(5) == att.end() && "T5 committed after checkpoint, must NOT be a loser!");
        assert(att.find(6) == att.end() && "T6 committed, must NOT be a loser!");
        assert(att.find(1) == att.end() && "T1 committed before checkpoint, must NOT be in ATT!");
        assert(!dpt.empty() && "DPT must contain dirty pages!");

        // Run Redo
        size_t redone = aries.Phase2Redo();
        std::cout << "  • [Redo Phase] Repeated history starting from min_recLSN (" << aries.GetMinRecLSN()
                  << "). Replayed " << redone << " operations." << std::endl;

        // Run Undo
        size_t undone = aries.Phase3Undo();
        std::cout << "  • [Undo Phase] Reversed " << undone << " loser T7 operations using CLRs." << std::endl;
        assert(undone >= 1 && "Loser T7 modifications must be rolled back!");

        // Validation
        auto table_heap = std::make_unique<TableHeap>(bpm.get(), inserted_rids[0].page_id);
        UserRow r1;
        assert(table_heap->GetTuple(inserted_rids[0], &r1));
        assert(r1.balance == 100.0 && "Row 1 must be reverted to original pre-T7 balance!");
        assert(std::string(r1.name) == "User_1" && "Row 1 name must be restored!");

        // Verify other rows
        for (size_t i = 1; i <= 6; ++i) {
            UserRow r;
            assert(table_heap->GetTuple(inserted_rids[i - 1], &r));
            assert(r.balance == 100.0 * i);
            assert(std::string(r.name) == ("User_" + std::to_string(i)));
        }

        std::cout << "  • Verification: Database state 100% consistent with bounded scan!" << std::endl;
    }

    CleanUpFiles(db_name, wal_name, meta_name);
    std::cout << "  ✓ Milestone 5 (ARIES Fuzzy Checkpointing) passed with flying colors!" << std::endl;
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
    std::cout << "DBMS RECOVERY SUITE: Write-Ahead Logging & ARIES Crash Recovery" << std::endl;
    if (test_num > 0) {
        std::cout << "Running Milestone Test #" << test_num << std::endl;
    }
    std::cout << "=================================================================" << std::endl;

    if (test_num == 0 || test_num == 1) TestLogRecordSerialization();
    if (test_num == 0 || test_num == 2) TestMechanicalSympathyGroupCommit();
    if (test_num == 0 || test_num == 3) TestCardinalWALInvariant();
    if (test_num == 0 || test_num == 4) TestCrashRecoveryRedoReplay();
    if (test_num == 0 || test_num == 5) TestFuzzyCheckpointRecovery();

    std::cout << "\n=================================================================" << std::endl;
    std::cout << "ALL RECOVERY TESTS COMPLETED WITH FLYING COLORS!" << std::endl;
    std::cout << "=================================================================" << std::endl;
    return 0;
}
