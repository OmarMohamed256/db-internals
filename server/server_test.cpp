#include <arpa/inet.h>
#include <cassert>
#include <chrono>
#include <cstring>
#include <iostream>
#include <memory>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include "../buffer_pool_manager_skeleton/async_disk_manager.cpp"
#include "../buffer_pool_manager_skeleton/buffer_pool_manager.cpp"
#include "../buffer_pool_manager_skeleton/disk_manager.cpp"
#include "../buffer_pool_manager_skeleton/io_uring_manager.cpp"
#include "../buffer_pool_manager_skeleton/lru_k_replacer.cpp"
#include "../storage/table_heap.cpp"
#include "session.h"
#include "sql_parser.h"
#include "thread_pool.h"

static void CleanUpFiles(const std::string &db_name, const std::string &wal_name) {
    std::remove(db_name.c_str());
    std::remove(wal_name.c_str());
    std::remove("test_idx.db");
}

// ============================================================================
// Milestone 1: SQL Tokenizer & Query Parser Tests
// ============================================================================
void TestSQLParser() {
    std::cout << "\n=== [Milestone 1] SQL Tokenizer & Query Parser ===" << std::endl;

    // 1. Transaction lifecycle commands
    auto s1 = SQLParser::Parse("BEGIN;");
    assert(s1.type == StatementType::BEGIN);
    std::cout << "  • Parsed BEGIN: StatementType::BEGIN" << std::endl;

    auto s2 = SQLParser::Parse("COMMIT;");
    assert(s2.type == StatementType::COMMIT);
    std::cout << "  • Parsed COMMIT: StatementType::COMMIT" << std::endl;

    auto s3 = SQLParser::Parse("ROLLBACK;");
    assert(s3.type == StatementType::ABORT);
    std::cout << "  • Parsed ROLLBACK: StatementType::ABORT" << std::endl;

    // 2. INSERT statement
    auto s4 = SQLParser::Parse("INSERT INTO users VALUES (42, 'Alice', 1250.50);");
    assert(s4.type == StatementType::INSERT);
    assert(s4.table_name == "users");
    assert(s4.insert_id == 42);
    assert(s4.insert_name == "Alice");
    assert(s4.insert_balance == 1250.50);
    std::cout << "  • Parsed INSERT: INSERT INTO users VALUES (" << s4.insert_id
              << ", '" << s4.insert_name << "', " << s4.insert_balance << ")" << std::endl;

    // 3. SELECT Point Query (Primary B+ Tree Index)
    auto s5 = SQLParser::Parse("SELECT * FROM users WHERE id = 42;");
    assert(s5.type == StatementType::SELECT);
    assert(s5.has_where);
    assert(s5.where_column == "id");
    assert(s5.where_op == "=");
    assert(s5.where_id_val == 42);
    std::cout << "  • Parsed Point Query: SELECT * FROM users WHERE id = 42" << std::endl;

    // 4. SELECT Range / Filter Query
    auto s6 = SQLParser::Parse("SELECT * FROM users WHERE balance > 500.00;");
    assert(s6.type == StatementType::SELECT);
    assert(s6.has_where);
    assert(s6.where_column == "balance");
    assert(s6.where_op == ">");
    assert(s6.where_num_val == 500.00);
    std::cout << "  • Parsed Range Query: SELECT * FROM users WHERE balance > 500.00" << std::endl;

    // 5. UPDATE Statement
    auto s7 = SQLParser::Parse("UPDATE users SET balance = 1500.00 WHERE id = 42;");
    assert(s7.type == StatementType::UPDATE);
    assert(s7.set_column == "balance");
    assert(s7.set_val == 1500.00);
    assert(s7.where_id_val == 42);
    std::cout << "  • Parsed UPDATE: UPDATE users SET balance = 1500.00 WHERE id = 42" << std::endl;

    std::cout << "  ✓ Milestone 1 (SQL Parser) passed!" << std::endl;
}

// ============================================================================
// Milestone 2: Client Session Execution & ASCII Formatter Tests
// ============================================================================
void TestClientSessionExecution() {
    std::cout << "\n=== [Milestone 2] Client Session Execution & ASCII Formatter ===" << std::endl;
    const std::string db_file = "test_session.db";
    const std::string wal_file = "test_session.wal";
    CleanUpFiles(db_file, wal_file);

    {
        auto disk_mgr = std::make_unique<AsyncDiskManager>(db_file);
        auto replacer = std::make_unique<LRUKReplacer>(100);
        auto bpm = std::make_unique<BufferPoolManager>(100, disk_mgr.get(), replacer.get());
        auto table_heap = std::make_unique<TableHeap>(bpm.get());
        BPlusTree<int64_t, RID> primary_index("test_idx.db", bpm.get());
        LogManager log_mgr(wal_file);
        TransactionManager txn_mgr;

        ClientSession session(bpm.get(), table_heap.get(), &primary_index, &txn_mgr, &log_mgr);

        // 1. Insert rows via session
        std::cout << "  • Executing: INSERT INTO users VALUES (1, 'Alice', 100.0);" << std::endl;
        std::string res1 = session.Execute("INSERT INTO users VALUES (1, 'Alice', 100.0);");
        assert(res1 == "INSERT 0 1\n");

        std::cout << "  • Executing: INSERT INTO users VALUES (2, 'Bob', 200.0);" << std::endl;
        std::string res2 = session.Execute("INSERT INTO users VALUES (2, 'Bob', 200.0);");
        assert(res2 == "INSERT 0 1\n");

        // 2. Point Index Query
        std::cout << "  • Executing Point Index Query: SELECT * FROM users WHERE id = 1;" << std::endl;
        std::string res_point = session.Execute("SELECT * FROM users WHERE id = 1;");
        std::cout << res_point;
        assert(res_point.find("Alice") != std::string::npos);

        // 3. Filter Range Query
        std::cout << "  • Executing Range Filter: SELECT * FROM users WHERE balance > 150.0;" << std::endl;
        std::string res_filter = session.Execute("SELECT * FROM users WHERE balance > 150.0;");
        std::cout << res_filter;
        assert(res_filter.find("Bob") != std::string::npos);
        assert(res_filter.find("Alice") == std::string::npos);

        std::cout << "  ✓ Milestone 2 (Session Execution & Formatter) passed!" << std::endl;
    }

    CleanUpFiles(db_file, wal_file);
}

// ============================================================================
// Milestone 3: Transaction Abort & Rollback in Session
// ============================================================================
void TestTransactionRollbackSession() {
    std::cout << "\n=== [Milestone 3] Transaction Abort & Rollback via Session ===" << std::endl;
    const std::string db_file = "test_rollback.db";
    const std::string wal_file = "test_rollback.wal";
    CleanUpFiles(db_file, wal_file);

    {
        auto disk_mgr = std::make_unique<AsyncDiskManager>(db_file);
        auto replacer = std::make_unique<LRUKReplacer>(100);
        auto bpm = std::make_unique<BufferPoolManager>(100, disk_mgr.get(), replacer.get());
        auto table_heap = std::make_unique<TableHeap>(bpm.get());
        BPlusTree<int64_t, RID> primary_index("test_idx.db", bpm.get());
        LogManager log_mgr(wal_file);
        TransactionManager txn_mgr;

        ClientSession session(bpm.get(), table_heap.get(), &primary_index, &txn_mgr, &log_mgr);

        std::cout << "  • Starting transaction: BEGIN;" << std::endl;
        assert(session.Execute("BEGIN;") == "BEGIN\n");
        assert(session.InTransaction());

        std::cout << "  • Inserting user 3 under transaction..." << std::endl;
        session.Execute("INSERT INTO users VALUES (3, 'Charlie', 300.0);");

        std::cout << "  • Rolling back: ABORT;" << std::endl;
        assert(session.Execute("ABORT;") == "ROLLBACK\n");
        assert(!session.InTransaction());

        std::cout << "  • Querying user 3: should not be found..." << std::endl;
        std::string res = session.Execute("SELECT * FROM users WHERE id = 3;");
        assert(res.find("Charlie") == std::string::npos);
        std::cout << "  ✓ Milestone 3 (Transaction Rollback in Session) passed!" << std::endl;
    }

    CleanUpFiles(db_file, wal_file);
}

// ============================================================================
// Main Test Dispatcher
// ============================================================================
int main(int argc, char **argv) {
    int test_num = 0;
    if (argc > 1) {
        test_num = std::atoi(argv[1]);
    }

    std::cout << "=================================================================" << std::endl;
    std::cout << "AETHERDB CLIENT/SERVER SUITE: Interactive SQL & TCP Server" << std::endl;
    if (test_num > 0) {
        std::cout << "Running Test #" << test_num << std::endl;
    }
    std::cout << "=================================================================" << std::endl;

    if (test_num == 0 || test_num == 1) TestSQLParser();
    if (test_num == 0 || test_num == 2) TestClientSessionExecution();
    if (test_num == 0 || test_num == 3) TestTransactionRollbackSession();

    std::cout << "\n=================================================================" << std::endl;
    std::cout << "ALL CLIENT/SERVER TESTS COMPLETED SUCCESSFULLY!" << std::endl;
    std::cout << "=================================================================" << std::endl;
    return 0;
}
